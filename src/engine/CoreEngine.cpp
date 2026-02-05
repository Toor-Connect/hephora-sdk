#include "CoreEngine.h"
#include "YamlDataLoader.h"
#include "YamlDataDecoder.h"

#include <stdexcept>
#include <utility>
#include <variant>
#include <unordered_set>
#include <deque>

// ----- lifecycle -----
void CoreEngine::init(const SchemaRegistry &registry)
{
    registry_ = &registry;
    backend_.init(registry);
}

// ----- tx -----
void CoreEngine::begin() { backend_.begin(); }
void CoreEngine::commit()
{
    backend_.commit();
}
void CoreEngine::rollback()
{
    backend_.rollback();
}

// ---------------- dirty helpers ----------------
void CoreEngine::markDirtyUpsert(const NodeKey &k)
{
    auto p = keyPair(k);
    dirty_upserts_.insert(p);
}

// ---------------- single-ops (journal-aware) ----------------
void CoreEngine::upsert(NodeSnapshot &node)
{
    bool isNew = false;
    std::optional<NodeSnapshot> res = std::nullopt;
    if (node.key.id.has_value())
    {
        res = backend_.fetch(node.key);
    }

    // Before update callback
    if (res.has_value() && before_node_update_callback_)
        before_node_update_callback_(node.key);

    backend_.upsert(node);

    if (res.has_value())
    {
        // Existing node updated: if label changed, old file must be deleted
        if (node.label != res->label && node.label.has_value() && !node.label->empty() &&
            res->label.has_value() && !res->label->empty())
        {
            purge_on_flush_.insert(cfg_.base_path + node.key.profile + "/" + *res->label + "_" + res->key.id->substr(0, 8) + ".yaml");
        }
        isNew = false;
    }
    else
    {
        isNew = true;
    }

    markDirtyUpsert(node.key);

    // Assert if node key id is null
    if (!node.key.id.has_value())
        throw std::runtime_error("On upsert, node key id is null");

    // Callbacks
    if (isNew && node_created_callback_)
        node_created_callback_(node.key);
    else if (!isNew && node_updated_callback_)
        node_updated_callback_(node.key);
}

void CoreEngine::remove(const NodeKey &key)
{
    if (!key.id.has_value())
        throw std::runtime_error("On remove, node key id is null");

    auto res = backend_.fetch(key);
    if (!res.has_value())
        throw std::runtime_error("Cannot remove: node not found");

    collectSubtreePurge(key);

    if (res->label.has_value() && !res->label->empty())
        purge_on_flush_.insert(cfg_.base_path + key.profile + "/" + *res->label + "_" + key.id->substr(0, 8) + ".yaml");

    auto affected = backend_.refsTo(key);

    backend_.remove(key);

    for (const auto &ownerKey : affected)
    {
        if (!backend_.exists(ownerKey))
            continue;

        auto snapOpt = backend_.fetch(ownerKey);
        if (!snapOpt)
            continue;

        markDirtyUpsert(ownerKey);
    }
}

// ---------------- reads (pass-through) ----------------
bool CoreEngine::exists(const NodeKey &key) const { return backend_.exists(key); }
std::optional<NodeSnapshot> CoreEngine::fetch(const NodeKey &key) const { return backend_.fetch(key); }
std::optional<std::string> CoreEngine::parentOf(const NodeKey &key) const { return backend_.parentOf(key); }
std::vector<NodeKey> CoreEngine::childrenOf(const NodeKey &parent, const std::string &childProfile) const
{
    return backend_.childrenOf(parent, childProfile);
}
std::vector<NodeKey> CoreEngine::refsFrom(const NodeKey &owner, const std::string &field) const
{
    return backend_.refsFrom(owner, field);
}
std::vector<NodeKey> CoreEngine::refsTo(const NodeKey &target) const { return backend_.refsTo(target); }
std::vector<NodeSnapshot> CoreEngine::find(const QueryDNF &q) const { return backend_.find(q); }

// ----- YAML persistence -----
void CoreEngine::flushPending()
{
    if (!committer_)
    {
        dirty_upserts_.clear();
        purge_on_flush_.clear();
        return;
    }

    for (const auto &f : purge_on_flush_)
    {
        committer_->deleteNode(f);
    }

    for (const auto &p : dirty_upserts_)
    {
        NodeKey k{p.first, p.second};
        auto snap = backend_.fetch(k);
        if (!snap.has_value())
            continue;
        committer_->writeNode(filename_of(snap.value()), *snap);
    }

    dirty_upserts_.clear();
    purge_on_flush_.clear();
}

void CoreEngine::flushAll()
{
    if (!committer_ || !registry_)
        return;

    for (const auto &f : purge_on_flush_)
    {
        committer_->deleteNode(f);
    }
    purge_on_flush_.clear();

    for (const auto &kv : registry_->schemas())
    {
        QueryDNF q;
        q.profile = kv.first;
        auto rows = backend_.find(q);
        for (const auto &snap : rows)
        {
            committer_->writeNode(filename_of(snap), snap);
        }
    }
}

// --- deterministic, root-anchored profile order (root, then its subtree, then leftovers) ---
static std::vector<std::string> computeProfileLoadOrder(const SchemaRegistry &reg)
{
    std::vector<std::string> order;
    std::unordered_set<std::string> seen;

    auto push_once = [&](const std::string &p)
    {
        if (seen.insert(p).second)
            order.push_back(p);
    };

    if (auto root = reg.root())
    {
        std::deque<const NodeSchema *> q;
        q.push_back(root);

        while (!q.empty())
        {
            const NodeSchema *cur = q.front();
            q.pop_front();
            push_once(cur->profileName());

            std::vector<std::pair<std::string, const NodeSchema *>> kids;
            kids.reserve(cur->children().size());
            for (const auto &kv : cur->children())
                kids.emplace_back(kv.first, kv.second.get());
            std::sort(kids.begin(), kids.end(),
                      [](auto &a, auto &b)
                      { return a.first < b.first; });

            for (auto &k : kids)
            {
                const NodeSchema *cs = k.second;
                if (cs && !seen.count(cs->profileName()))
                    q.push_back(cs);
            }
        }
    }

    std::vector<std::string> remaining;
    remaining.reserve(reg.schemas().size());
    for (const auto &kv : reg.schemas())
        if (!seen.count(kv.first))
            remaining.push_back(kv.first);
    std::sort(remaining.begin(), remaining.end());
    for (const auto &p : remaining)
        push_once(p);

    return order;
}

void CoreEngine::loadData(const std::unordered_map<std::string, YAML::Node> &docs)
{
    if (!registry_)
        throw std::runtime_error("CoreEngine.loadData: call init(registry) first");

    // 1) Decode YAML docs into typed nodes (no storage effects yet)
    auto decoded = YamlDataDecoder::decode(docs, *registry_);

    // Helper: detect array<reference> fields for a (profile, fieldName)
    auto isRefArrayField = [&](const std::string &profile, const std::string &fname) -> bool
    {
        const NodeSchema *s = registry_->getSchema(profile);
        if (!s)
            return false;
        const FieldSchema *fs = s->getField(fname);
        if (!fs || fs->type() != FieldType::Array)
            return false;
        auto as = dynamic_cast<const ArrayFieldSchema *>(fs);
        return as && as->items() && as->items()->type() == FieldType::Reference;
    };

    // Split into:
    //  - initial snapshots without any array<reference> fields
    //  - a list of ref arrays to set later once all rows exist
    struct PendingRefArray
    {
        NodeKey key;
        std::string field;
        ArrayData values;
    };

    std::vector<NodeSnapshot> initial;
    std::vector<PendingRefArray> later;
    initial.reserve(decoded.size());

    for (const auto &dn : decoded)
    {
        NodeSnapshot ns;
        // Treat empty strings as "not provided"
        std::optional<std::string> idOpt;
        if (!dn.id.empty())
            idOpt = dn.id;

        std::optional<std::string> labelOpt;
        if (!dn.label.empty())
            labelOpt = dn.label;

        std::optional<std::string> parentOpt;
        if (!dn.parent_id.empty())
            parentOpt = dn.parent_id;

        ns.key = NodeKey{dn.profile, idOpt};
        ns.label = labelOpt;
        ns.parent_id = parentOpt;

        for (const auto &kv : dn.instance.fields)
        {
            const std::string &fname = kv.first;
            const FieldValue &fval = kv.second;
            if (isRefArrayField(dn.profile, fname))
            {
                if (fval.isArray())
                    later.push_back(PendingRefArray{ns.key, fname, fval.asArray()});
            }
            else
            {
                ns.fields.emplace(fname, fval);
            }
        }
        initial.push_back(std::move(ns));
    }

    // 2) Insert roots→children (so _parent_id FKs resolve)
    const auto order = computeProfileLoadOrder(*registry_);
    std::unordered_map<std::string, std::vector<NodeSnapshot>> byProfile;
    for (auto &ns : initial)
        byProfile[ns.key.profile].push_back(std::move(ns));

    backend_.setReferenceChecksEnabled(false);
    begin();
    try
    {
        for (const auto &prof : order)
        {
            auto it = byProfile.find(prof);
            if (it == byProfile.end())
                continue;

            for (auto &ns : it->second)
            {
                backend_.upsert(ns);
                markDirtyUpsert(ns.key);
            }
        }
        commit();
    }
    catch (...)
    {
        rollback();
        throw;
    }
    backend_.setReferenceChecksEnabled(true);

    // Optional sanity: verify first-pass rows exist
    for (const auto &kv : byProfile)
    {
        for (const auto &ns : kv.second)
        {
            if (!ns.key.id.has_value())
                throw std::runtime_error("loadData(pass1): row has no _id after insert: " +
                                         ns.key.profile + "/<null>");
            if (!backend_.exists(ns.key))
                throw std::runtime_error("loadData(pass1): row missing after insert: " +
                                         ns.key.profile + "/" + *ns.key.id);
        }
    }

    // 3) Set array<reference> fields now that targets exist
    if (!later.empty())
    {
        begin();
        try
        {
            for (auto &pr : later)
            {
                if (!pr.key.id.has_value())
                    throw std::runtime_error("loadData(pass2): owner has no _id for ref-array write: " +
                                             pr.key.profile + "/<null> field=" + pr.field);

                if (!backend_.exists(pr.key))
                    throw std::runtime_error("loadData(pass2): owner missing before ref-array write: " +
                                             pr.key.profile + "/" + *pr.key.id +
                                             " field=" + pr.field);

                const NodeSchema *ownerSchema = registry_->getSchema(pr.key.profile);
                const FieldSchema *fs = ownerSchema ? ownerSchema->getField(pr.field) : nullptr;
                auto as = (fs && fs->type() == FieldType::Array)
                              ? dynamic_cast<const ArrayFieldSchema *>(fs)
                              : nullptr;
                auto rf = (as && as->items() && as->items()->type() == FieldType::Reference)
                              ? dynamic_cast<const ReferenceFieldSchema *>(as->items())
                              : nullptr;
                if (!rf)
                    throw std::runtime_error("loadData(pass2): field is not array<reference> on " +
                                             pr.key.profile + "." + pr.field);

                auto cur = backend_.fetch(pr.key);
                if (!cur)
                    throw std::runtime_error("loadData(pass2): fetch failed for owner " +
                                             pr.key.profile + "/" + *pr.key.id);

                cur->fields[pr.field] = FieldValue(pr.values);
                backend_.upsert(*cur);
                markDirtyUpsert(pr.key);
            }
            commit();
        }
        catch (...)
        {
            rollback();
            throw;
        }
    }

    try
    {
        backend_.validateData();
    }
    catch (const std::exception &ex)
    {
        resetBackend();
        throw; // or throw a new exception with more info
    }
}

void CoreEngine::resetBackend(void)
{
    dirty_upserts_.clear();
    purge_on_flush_.clear();
    backend_.reset();
}

void CoreEngine::collectSubtreePurge(const NodeKey &root)
{
    if (!registry_)
        return;

    const NodeSchema *s = registry_->getSchema(root.profile);
    if (!s)
        return;

    // For each declared child profile of this profile
    for (const auto &kv : s->children())
    {
        const std::string childProfile = kv.second->profileName();

        // Load children IDs from DB
        auto kids = backend_.childrenOf(root, childProfile);
        for (const auto &ck : kids)
        {
            // Get label now (it'll be gone after remove)
            if (auto snap = backend_.fetch(ck))
            {
                if (snap->label.has_value() && !snap->label->empty())
                {
                    purge_on_flush_.insert(
                        cfg_.base_path + ck.profile + "/" + *snap->label + "_" + ck.id->substr(0, 8) + ".yaml");
                }
            }

            // Recurse to grandchildren
            collectSubtreePurge(ck);
        }
    }
}
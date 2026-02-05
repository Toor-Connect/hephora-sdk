#pragma once
#include "ICoreEngine.h"
#include <set>

class CoreEngine final : public ICoreEngine
{
public:
    const SchemaRegistry *getRegistry() const { return registry_; }
    void setNodeCreatedCallback(std::function<void(const NodeKey &)> cb) { node_created_callback_ = cb; }
    std::function<void(const NodeKey &)> getNodeCreatedCallback() const { return node_created_callback_; }
    void setBeforeNodeUpdateCallback(std::function<void(const NodeKey &)> cb) { before_node_update_callback_ = cb; }
    std::function<void(const NodeKey &)> getBeforeNodeUpdateCallback() const { return before_node_update_callback_; }
    void setNodeUpdatedCallback(std::function<void(const NodeKey &)> cb) { node_updated_callback_ = cb; }
    std::function<void(const NodeKey &)> getNodeUpdatedCallback() const { return node_updated_callback_; }
    CoreEngine(IDataBackend &backend,
               IDataCommit *committer = nullptr,
               EngineConfig cfg = {})
        : backend_(backend), committer_(committer), cfg_(cfg) {}

    // lifecycle
    void init(const SchemaRegistry &registry) override;

    // edits
    void upsert(NodeSnapshot &node) override;
    void remove(const NodeKey &key) override;

    // reads
    bool exists(const NodeKey &key) const override;
    std::optional<NodeSnapshot> fetch(const NodeKey &key) const override;
    std::optional<std::string> parentOf(const NodeKey &key) const override;
    std::vector<NodeKey> childrenOf(const NodeKey &parent,
                                    const std::string &childProfile) const override;
    std::vector<NodeKey> refsFrom(const NodeKey &owner,
                                  const std::string &field) const override;
    std::vector<NodeKey> refsTo(const NodeKey &target) const override;
    std::vector<NodeSnapshot> find(const QueryDNF &q) const override;

    // YAML persistence
    void flushPending() override;
    void flushAll() override;

    // YAML data
    void loadData(const std::unordered_map<std::string, YAML::Node> &docs) override;

    void resetBackend(void) override;

private:
    void begin();
    void commit();
    void rollback();
    void collectSubtreePurge(const NodeKey &root);
    // helpers for dirty tracking
    static std::pair<std::string, std::string> keyPair(const NodeKey &k)
    {
        if (!k.id.has_value())
            throw std::runtime_error("keyPair: NodeKey._id is not provided");
        return {k.profile, *k.id};
    }

    // Filename policy for YAML persistence (default: "<profile>/<id>.yaml")
    std::string filename_of(const NodeSnapshot &k)
    {
        if (!k.label.has_value() || k.label->empty())
            throw std::runtime_error("filename_of: NodeSnapshot._label is not provided");
        if (!k.key.id.has_value())
            throw std::runtime_error("filename_of: NodeSnapshot._id is not provided");
        std::string id_prefix = k.key.id->substr(0, 8);
        return cfg_.base_path + k.key.profile + "/" + *k.label + "_" + id_prefix + ".yaml";
    }

    void markDirtyUpsert(const NodeKey &k);

private:
    IDataBackend &backend_;
    IDataCommit *committer_; // optional: writes YAML when flushing
    EngineConfig cfg_;
    const SchemaRegistry *registry_{nullptr};
    // dirty tracking (by profile,id pairs)
    std::set<std::pair<std::string, std::string>> dirty_upserts_;
    std::set<std::string> purge_on_flush_;
    // Callback on create/update of nodes (for external tracking, e.g. Lua)
    std::function<void(const NodeKey &key)> node_created_callback_{nullptr};
    std::function<void(const NodeKey &key)> before_node_update_callback_{nullptr};
    std::function<void(const NodeKey &key)> node_updated_callback_{nullptr};

    friend class LuaEngine; // to set callbacks
};

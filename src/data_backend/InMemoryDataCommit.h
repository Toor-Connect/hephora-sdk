#pragma once
#include <unordered_map>
#include <string>
#include <yaml-cpp/yaml.h>
#include "IDataCommit.h"
#include "FieldValue.h"
#include "NodeAddress.h"

// Minimal in-memory committer that serializes NodeSnapshot to YAML text
// and stores it by filename in an internal map. Great for tests and "virtual fs".
class InMemoryDataCommit final : public IDataCommit
{
public:
    void writeNode(const std::string &filename,
                   const NodeSnapshot &node) override;

    void deleteNode(const std::string &filename) override;

    // For tests/inspection
    const std::unordered_map<std::string, std::string> &storage() const { return files_; }
    bool has(const std::string &filename) const { return files_.find(filename) != files_.end(); }
    const std::string &get(const std::string &filename) const { return files_.at(filename); }

private:
    static YAML::Node toYaml(const FieldValue &v);
    static YAML::Node toYamlFieldsMap(const std::map<std::string, FieldValue> &fields);

    std::unordered_map<std::string, std::string> files_;
};

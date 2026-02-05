#pragma once
#include <string>
#include "IDataCommit.h"
#include "YamlDataEncoder.h"

class FilesystemDataCommit final : public IDataCommit
{
public:
    explicit FilesystemDataCommit(std::string root_dir, std::string default_ext = ".yaml")
        : root_dir_(std::move(root_dir)), default_ext_(std::move(default_ext)) {}

    void writeNode(const std::string &filename, const NodeSnapshot &node) override;
    void deleteNode(const std::string &filename) override;

    // Optional: expose the path resolver if you want to share it with EngineConfig
    std::string resolvePath(const std::string &filename, const NodeKey &key) const;

private:
    std::string root_dir_;
    std::string default_ext_;
    YamlDataEncoder encoder_;
};

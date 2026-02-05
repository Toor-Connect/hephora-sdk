#include "FilesystemDataCommit.h"
#include <fstream>
#include <stdexcept>
#include <filesystem>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

std::string FilesystemDataCommit::resolvePath(const std::string &filename, const NodeKey &key) const
{
    // If caller supplied an absolute path, honor it.
    if (!filename.empty() && fs::path(filename).is_absolute())
        return filename;

    // Compute default relative path if needed.
    std::string rel = filename;
    if (rel.empty())
    {
        if (!key.id.has_value() || key.id->empty())
            throw std::runtime_error("resolvePath: filename is empty and NodeKey._id is not provided");
        rel = key.profile + "/" + *key.id + default_ext_;
    }

    fs::path p = fs::path(root_dir_) / rel;
    return p.lexically_normal().string();
}
void FilesystemDataCommit::writeNode(const std::string &filename, const NodeSnapshot &node)
{
    const std::string path = resolvePath(filename, node.key);

    // Ensure parent directory exists
    const fs::path p(path);
    if (p.has_parent_path())
    {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        if (ec)
        {
            throw std::runtime_error("FilesystemDataCommit: create_directories failed for '" +
                                     p.parent_path().string() + "': " + ec.message());
        }
    }

    // Encode to YAML
    YAML::Node root = encoder_.encodeNode(node);
    YAML::Emitter out;
    out.SetIndent(2);
    out << root;

    // Write atomically-ish: write to temp, then replace
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs)
            throw std::runtime_error("FilesystemDataCommit: cannot open temp file: " + tmp.string());
        ofs << out.c_str();
        ofs.flush();
        if (!ofs)
            throw std::runtime_error("FilesystemDataCommit: write failed: " + tmp.string());
    }

    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec)
    {
        // On some filesystems, rename across devices fails—fallback to copy+replace
        fs::remove(p, ec); // ignore error
        std::error_code ec2;
        fs::rename(tmp, p, ec2);
        if (ec2)
        {
            // final fallback: copy_file
            std::error_code ec3;
            fs::copy_file(tmp, p, fs::copy_options::overwrite_existing, ec3);
            fs::remove(tmp, ec); // cleanup
            if (ec3)
                throw std::runtime_error("FilesystemDataCommit: rename/copy failed to '" +
                                         p.string() + "': " + ec3.message());
        }
    }
}

void FilesystemDataCommit::deleteNode(const std::string &filename)
{
    // For deletes, we don't know the NodeKey; resolve with empty key into default path
    // The engine should pass a concrete path; but if empty, do nothing.
    if (filename.empty())
        return;

    fs::path p(filename);
    if (!p.is_absolute())
        p = fs::path(root_dir_) / p;

    std::error_code ec;
    fs::remove(p, ec);
    if (ec && ec.value() != static_cast<int>(std::errc::no_such_file_or_directory))
    {
        throw std::runtime_error("FilesystemDataCommit: remove failed for '" +
                                 p.string() + "': " + ec.message());
    }
}

#include "YamlSchemaLoader.h"
#include "YamlSchemaDecoder.h"
#include "SchemaRegistry.h"

#include <stdexcept>

std::vector<YAML::Node> YamlSchemaLoader::loadFromSources(
    const std::unordered_map<std::string, std::string> &sources)
{
    std::vector<YAML::Node> result;
    result.reserve(sources.size());

    for (const auto &[filename, contents] : sources)
    {
        try
        {
            YAML::Node node = YAML::Load(contents);
            node.SetTag("!" + filename);
            result.push_back(node);
        }
        catch (const YAML::ParserException &ex)
        {
            throw std::runtime_error("YAML parse error in [" + filename + "]: " + ex.what());
        }
    }
    return result;
}

void YamlSchemaLoader::loadIntoRegistry(
    const std::unordered_map<std::string, std::string> &sources,
    SchemaRegistry &registry)
{
    auto nodes = loadFromSources(sources);
    YamlSchemaDecoder::decodeProfiles(nodes, registry);
}

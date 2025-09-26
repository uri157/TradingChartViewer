#pragma once

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

class ResourceProvider {
public:
    struct FontResource {
        std::shared_ptr<sf::Font> font;
        bool ready{false};
    };

    struct TextureResource {
        std::shared_ptr<sf::Texture> texture;
        bool ready{false};
    };

    ResourceProvider();

    FontResource getFontResource(const std::string& key);
    TextureResource getTextureResource(const std::string& key);

    std::shared_ptr<sf::Font> getFont(const std::string& key);
    std::shared_ptr<sf::Texture> getTexture(const std::string& key);

private:
    FontResource loadFontUnlocked(const std::string& key);
    TextureResource loadTextureUnlocked(const std::string& key);
    std::vector<std::filesystem::path> candidateFontPaths(const std::string& key) const;
    std::vector<std::filesystem::path> candidateTexturePaths(const std::string& key) const;

    std::unordered_map<std::string, FontResource> fonts;
    std::unordered_map<std::string, TextureResource> textures;
    std::mutex fontMutex;
    std::mutex textureMutex;
    std::filesystem::path projectRoot;
};

}  // namespace ui

#include "ui/ResourceProvider.h"

#include "logging/Log.h"

#include <array>

namespace {

sf::Image createFallbackCursorImage() {
    sf::Image image;
    constexpr unsigned size = 32;
    image.create(size, size, sf::Color::Transparent);
    const sf::Color color(240, 240, 240, 220);
    for (unsigned i = 0; i < size; ++i) {
        image.setPixel(size / 2, i, color);
        image.setPixel(i, size / 2, color);
    }
    image.setPixel(size / 2, size / 2, sf::Color::Red);
    return image;
}

} // namespace

namespace ui {

ResourceProvider::ResourceProvider()
    : projectRoot(std::filesystem::current_path()) {}

ResourceProvider::FontResource ResourceProvider::getFontResource(const std::string& key) {
    std::lock_guard<std::mutex> lock(fontMutex);
    if (auto it = fonts.find(key); it != fonts.end()) {
        return it->second;
    }
    auto loaded = loadFontUnlocked(key);
    fonts.emplace(key, loaded);
    return loaded;
}

ResourceProvider::TextureResource ResourceProvider::getTextureResource(const std::string& key) {
    std::lock_guard<std::mutex> lock(textureMutex);
    if (auto it = textures.find(key); it != textures.end()) {
        return it->second;
    }
    auto loaded = loadTextureUnlocked(key);
    textures.emplace(key, loaded);
    return loaded;
}

std::shared_ptr<sf::Font> ResourceProvider::getFont(const std::string& key) {
    return getFontResource(key).font;
}

std::shared_ptr<sf::Texture> ResourceProvider::getTexture(const std::string& key) {
    return getTextureResource(key).texture;
}

ResourceProvider::FontResource ResourceProvider::loadFontUnlocked(const std::string& key) {
    FontResource resource;
    resource.font = std::make_shared<sf::Font>();
    for (const auto& path : candidateFontPaths(key)) {
        if (std::filesystem::exists(path) && resource.font->loadFromFile(path.string())) {
            LOG_DEBUG(logging::LogCategory::UI, "Loaded font from %s", path.string().c_str());
            resource.ready = true;
            return resource;
        }
    }

    LOG_WARN(logging::LogCategory::UI, "Falling back to system font for key %s", key.c_str());
    static const std::array<const char*, 3> systemCandidates{
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
    };

    for (const char* candidate : systemCandidates) {
        if (std::filesystem::exists(candidate) && resource.font->loadFromFile(candidate)) {
            LOG_INFO(logging::LogCategory::UI, "Loaded system font from %s", candidate);
            resource.ready = true;
            return resource;
        }
    }

    LOG_ERROR(logging::LogCategory::UI, "Unable to load font for key %s. Text rendering will be degraded.", key.c_str());
    return resource;
}

ResourceProvider::TextureResource ResourceProvider::loadTextureUnlocked(const std::string& key) {
    TextureResource resource;
    resource.texture = std::make_shared<sf::Texture>();
    for (const auto& path : candidateTexturePaths(key)) {
        if (std::filesystem::exists(path) && resource.texture->loadFromFile(path.string())) {
            LOG_DEBUG(logging::LogCategory::RENDER, "Loaded texture from %s", path.string().c_str());
            resource.ready = true;
            return resource;
        }
    }

    if (key == "cursor") {
        LOG_WARN(logging::LogCategory::UI, "Cursor texture not found. Using procedural fallback.");
        if (resource.texture->loadFromImage(createFallbackCursorImage())) {
            resource.ready = true;
        }
        return resource;
    }

    LOG_ERROR(logging::LogCategory::RENDER, "Unable to load texture for key %s", key.c_str());
    return resource;
}

std::vector<std::filesystem::path> ResourceProvider::candidateFontPaths(const std::string& key) const {
    std::vector<std::filesystem::path> paths;
    if (key == "ui" || key == "default") {
        paths.emplace_back(projectRoot / "assets" / "Calibri.ttf");
        paths.emplace_back(projectRoot / "resources" / "Calibri.ttf");
        paths.emplace_back(projectRoot / "Calibri.ttf");
    }
    else {
        paths.emplace_back(projectRoot / key);
        paths.emplace_back(projectRoot / "assets" / key);
        paths.emplace_back(projectRoot / "resources" / key);
    }
    return paths;
}

std::vector<std::filesystem::path> ResourceProvider::candidateTexturePaths(const std::string& key) const {
    std::vector<std::filesystem::path> paths;
    if (key == "cursor") {
        paths.emplace_back(projectRoot / "assets" / "cursorSprite_0.png");
        paths.emplace_back(projectRoot / "resources" / "cursorSprite_0.png");
        paths.emplace_back(projectRoot / "cursorSprite_0.png");
    }
    else {
        paths.emplace_back(projectRoot / key);
        paths.emplace_back(projectRoot / "assets" / key);
        paths.emplace_back(projectRoot / "resources" / key);
    }
    return paths;
}

}  // namespace ui

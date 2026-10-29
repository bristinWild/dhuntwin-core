#include "dhun/catalog.h"

#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace dhun::catalog {

namespace detail {
struct Entry {
    const char* name;
    const unsigned char* data;
    std::size_t size;
};
extern const Entry kEntries[];
extern const std::size_t kEntryCount;
}  // namespace detail

namespace {

const detail::Entry* find(std::string_view name)
{
    for (std::size_t i = 0; i < detail::kEntryCount; ++i) {
        if (name == detail::kEntries[i].name) return &detail::kEntries[i];
    }
    return nullptr;
}

std::mutex& cacheMutex()
{
    static std::mutex m;
    return m;
}

std::map<std::string, std::unique_ptr<Json>, std::less<>>& cache()
{
    static std::map<std::string, std::unique_ptr<Json>, std::less<>> c;
    return c;
}

}  // namespace

std::string_view raw(std::string_view name)
{
    const auto* entry = find(name);
    if (!entry) throw std::out_of_range("unknown catalog dataset: " + std::string(name));
    return {reinterpret_cast<const char*>(entry->data), entry->size};
}

const Json& get(std::string_view name)
{
    std::lock_guard<std::mutex> lock(cacheMutex());
    auto& c = cache();
    auto it = c.find(name);
    if (it != c.end()) return *it->second;

    auto parsed = std::make_unique<Json>(Json::parse(raw(name)));
    const Json& ref = *parsed;
    c.emplace(std::string(name), std::move(parsed));
    return ref;
}

std::vector<std::string> names()
{
    std::vector<std::string> out;
    for (std::size_t i = 0; i < detail::kEntryCount; ++i) out.emplace_back(detail::kEntries[i].name);
    return out;
}

}  // namespace dhun::catalog

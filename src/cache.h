#pragma once

#include <winmd_reader.h>
using namespace winmd::reader;

namespace win32 {
    struct cache {
        cache() = default;
        cache(cache const&) = delete;
        cache& operator=(cache const&) = delete;

        explicit cache(std::string_view const& file)
            : m_database(file)
        {
            auto& db = m_database;
            for (auto&& type : db.TypeDef) {
                if (type.Flags().value == 0 || is_nested(type)) {
                    continue;
                }
                auto& ns = m_namespaces[type.TypeNamespace()];
                ns.try_emplace(type.TypeName(), type);
            }
            for (auto&& row : db.NestedClass) {
                m_nested_types[row.EnclosingType()].push_back(row.NestedType());
            }
            for (auto&& impl : db.ImplMap) {
                m_apis.try_emplace(impl.ImportName(), impl);
            }
            for (auto&& field : db.Field) {
                m_constants.try_emplace(field.Name(), field.Constant());
            }
        }

        TypeDef find(std::string_view const& type_namespace, std::string_view const& type_name) const noexcept {
            auto ns = m_namespaces.find(type_namespace);
            if (ns == m_namespaces.end()) {
                return {};
            }
            auto type = ns->second.find(type_name);
            if (type == ns->second.end()) {
                return {};
            }
            return type->second;
        }

        TypeDef find(std::string_view const& type_string) const {
            auto pos = type_string.rfind('.');
            if (pos == std::string_view::npos) {
                throw_invalid("Type '", type_string, "' is missing a namespace qualifier");
            }
            return find(type_string.substr(0, pos), type_string.substr(pos + 1, type_string.size()));
        }

        TypeDef find_required(std::string_view const& type_namespace, std::string_view const& type_name) const {
            auto definition = find(type_namespace, type_name);
            if (!definition) {
                throw_invalid("Type '", type_namespace, ".", type_name, "' could not be found");
            }
            return definition;
        }

        TypeDef find_required(std::string_view const& type_string) const {
            auto pos = type_string.rfind('.');
            if (pos == std::string_view::npos) {
                throw_invalid("Type '", type_string, "' is missing a namespace qualifier");
            }
            return find_required(type_string.substr(0, pos), type_string.substr(pos + 1, type_string.size()));
        }

        auto find_api(std::string_view const& name) const {
            return tfind(m_apis, name);
        }

        auto find_constant(std::string_view const& name) const {
            return tfind(m_constants, name);
        }

        auto const& database() const noexcept {
            return m_database;
        }

        auto const& namespaces() const noexcept {
            return m_namespaces;
        }

        std::vector<TypeDef> const& nested_types(TypeDef const& enclosing_type) const {
            auto it = m_nested_types.find(enclosing_type);
            if (it != m_nested_types.end()) {
                return it->second;
            }
            else {
                static const std::vector<TypeDef> empty;
                return empty;
            }
        }

        template <typename...T>
        [[noreturn]] static inline void throw_invalid(std::string message, T const&... args) {
            (message.append(args), ...);
            throw std::invalid_argument(message);
        }

        using namespace_members = std::map<std::string_view, TypeDef>;
        using namespace_type = std::pair<std::string_view const, namespace_members> const&;

    private:
        template <typename T>
        static const T* tfind(std::map<std::string_view, T> const& m, std::string_view const& name) noexcept {
            auto it = m.find(name);
            if (it == m.end()) {
                return nullptr;
            }
            return &it->second;
        }

    private:
        winmd::reader::database m_database;
        std::map<std::string_view, namespace_members> m_namespaces;
        std::map<TypeDef, std::vector<TypeDef>> m_nested_types;
        std::map<std::string_view, ImplMap> m_apis;
        std::map<std::string_view, Constant> m_constants;
    };
}

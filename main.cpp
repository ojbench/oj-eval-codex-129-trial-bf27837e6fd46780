#include <charconv>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

using std::string;
using std::string_view;

struct Value {
    bool is_int = true;
    long long int_value = 0;
    string string_value;
};

struct TransparentHash {
    using is_transparent = void;

    size_t operator()(string_view value) const noexcept {
        return std::hash<string_view>{}(value);
    }

    size_t operator()(const string &value) const noexcept {
        return std::hash<string>{}(value);
    }
};

struct TransparentEqual {
    using is_transparent = void;

    bool operator()(string_view lhs, string_view rhs) const noexcept {
        return lhs == rhs;
    }

    bool operator()(const string &lhs, const string &rhs) const noexcept {
        return lhs == rhs;
    }

    bool operator()(const string &lhs, string_view rhs) const noexcept {
        return lhs == rhs;
    }

    bool operator()(string_view lhs, const string &rhs) const noexcept {
        return lhs == rhs;
    }
};

using Scope = std::unordered_map<string, std::unique_ptr<Value>, TransparentHash, TransparentEqual>;
using ActiveBindings = std::unordered_map<string, std::vector<Value *>, TransparentHash, TransparentEqual>;

static std::vector<string_view> tokenize(const string &line) {
    std::vector<string_view> tokens;
    size_t position = 0;
    while (position < line.size()) {
        while (position < line.size() && line[position] == ' ') {
            ++position;
        }
        if (position >= line.size()) {
            break;
        }
        size_t start = position;
        if (line[position] == '"') {
            ++position;
            while (position < line.size() && line[position] != '"') {
                ++position;
            }
            if (position < line.size()) {
                ++position;
            }
            tokens.emplace_back(line.data() + start, position - start);
        } else {
            while (position < line.size() && line[position] != ' ') {
                ++position;
            }
            tokens.emplace_back(line.data() + start, position - start);
        }
    }
    return tokens;
}

static bool parse_int(string_view token, long long &value) {
    const char *begin = token.data();
    const char *end = token.data() + token.size();
    auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

static bool is_string_literal(string_view token) {
    return token.size() >= 2 && token.front() == '"' && token.back() == '"';
}

static string unquote(string_view token) {
    return string(token.substr(1, token.size() - 2));
}

static Value *lookup(ActiveBindings &active, string_view name) {
    auto it = active.find(name);
    if (it == active.end() || it->second.empty()) {
        return nullptr;
    }
    return it->second.back();
}

static bool valid_declaration_value(string_view type, string_view value_token, Value &value) {
    if (type == "int") {
        long long int_value = 0;
        if (!parse_int(value_token, int_value)) {
            return false;
        }
        value.is_int = true;
        value.int_value = int_value;
        value.string_value.clear();
        return true;
    }
    if (!is_string_literal(value_token)) {
        return false;
    }
    value.is_int = false;
    value.string_value = unquote(value_token);
    return true;
}

static bool valid_literal_for_existing_value(const Value &target, string_view value_token, Value &value) {
    if (target.is_int) {
        long long int_value = 0;
        if (!parse_int(value_token, int_value)) {
            return false;
        }
        value.is_int = true;
        value.int_value = int_value;
        value.string_value.clear();
        return true;
    }
    if (!is_string_literal(value_token)) {
        return false;
    }
    value.is_int = false;
    value.string_value = unquote(value_token);
    return true;
}

static void assign_value(Value &target, const Value &source) {
    target.is_int = source.is_int;
    target.int_value = source.int_value;
    target.string_value = source.string_value;
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    int n = 0;
    if (!(std::cin >> n)) {
        return 0;
    }
    string line;
    std::getline(std::cin, line);

    std::vector<Scope> scopes(1);
    ActiveBindings active;

    for (int i = 0; i < n; ++i) {
        std::getline(std::cin, line);
        auto tokens = tokenize(line);
        if (tokens.empty()) {
            continue;
        }

        const string_view command = tokens[0];
        bool valid = true;

        if (command == "Indent") {
            scopes.emplace_back();
        } else if (command == "Dedent") {
            if (scopes.size() == 1) {
                valid = false;
            } else {
                Scope &current_scope = scopes.back();
                for (auto &entry : current_scope) {
                    auto it = active.find(entry.first);
                    if (it != active.end()) {
                        it->second.pop_back();
                        if (it->second.empty()) {
                            active.erase(it);
                        }
                    }
                }
                scopes.pop_back();
            }
        } else if (command == "Declare") {
            if (tokens.size() != 4) {
                valid = false;
            } else {
                const string_view type = tokens[1];
                const string_view name = tokens[2];
                const string_view value_token = tokens[3];
                if (scopes.back().find(string(name)) != scopes.back().end()) {
                    valid = false;
                } else {
                    Value value;
                    if (!valid_declaration_value(type, value_token, value)) {
                        valid = false;
                    } else {
                        auto [it, inserted] = scopes.back().emplace(string(name), std::make_unique<Value>(std::move(value)));
                        (void)inserted;
                        active[it->first].push_back(it->second.get());
                    }
                }
            }
        } else if (command == "Add") {
            if (tokens.size() != 4) {
                valid = false;
            } else {
                Value *result = lookup(active, tokens[1]);
                Value *lhs = lookup(active, tokens[2]);
                Value *rhs = lookup(active, tokens[3]);
                if (!result || !lhs || !rhs || result->is_int != lhs->is_int || lhs->is_int != rhs->is_int) {
                    valid = false;
                } else {
                    Value updated;
                    updated.is_int = result->is_int;
                    if (updated.is_int) {
                        updated.int_value = lhs->int_value + rhs->int_value;
                    } else {
                        updated.string_value = lhs->string_value + rhs->string_value;
                    }
                    assign_value(*result, updated);
                }
            }
        } else if (command == "SelfAdd") {
            if (tokens.size() != 3) {
                valid = false;
            } else {
                Value *target = lookup(active, tokens[1]);
                if (!target) {
                    valid = false;
                } else {
                    Value addition;
                    if (!valid_literal_for_existing_value(*target, tokens[2], addition)) {
                        valid = false;
                    } else if (target->is_int) {
                        target->int_value += addition.int_value;
                    } else {
                        target->string_value += addition.string_value;
                    }
                }
            }
        } else if (command == "Print") {
            if (tokens.size() != 2) {
                valid = false;
            } else {
                Value *target = lookup(active, tokens[1]);
                if (!target) {
                    valid = false;
                } else {
                    std::cout << tokens[1] << ':';
                    if (target->is_int) {
                        std::cout << target->int_value;
                    } else {
                        std::cout << target->string_value;
                    }
                    std::cout << '\n';
                }
            }
        }

        if (!valid) {
            std::cout << "Invalid operation\n";
        }
    }

    return 0;
}

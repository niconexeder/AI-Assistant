#pragma once

#include <string>

namespace ba {

enum class Role {
    User,
    Assistant
};

struct Message {
    Role Speaker = Role::User;
    std::string Text;
};

} // namespace ba

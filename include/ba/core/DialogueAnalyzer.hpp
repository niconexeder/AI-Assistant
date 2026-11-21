#pragma once

#include <vector>

#include "ba/core/MessageInterpreter.hpp"
#include "ba/models/Message.hpp"
#include "ba/models/Requirements.hpp"

namespace ba {

class DialogueAnalyzer {
public:
    DialogueAnalyzer(const CityDirectory& cities, const CategoryDirectory& categories);

    Requirements Analyze(const std::vector<Message>& messages) const;

private:
    MessageInterpreter Interpreter_;
};

} // namespace ba

#ifndef GOOD_HPP
#define GOOD_HPP
#include <string>

namespace app {
using Name = std::string;

struct Base {
    void run(int value);
};

struct Derived : Base {
    using Base::run;
    void run(const Name &name);
};
}
#endif

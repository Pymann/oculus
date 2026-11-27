#include <memory>

class Mask {
public:
    explicit Mask(unsigned bits) : bits_(bits) {}
    Mask operator&(const Mask &other) const { return Mask(bits_ & other.bits_); }
private:
    unsigned bits_;
};

const Mask *where(const Mask &mask) { return std::addressof(mask); }

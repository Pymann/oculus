struct Condition {
    bool value;
    explicit operator bool() const { return value; }
};

bool both(const Condition &left, const Condition &right)
{
    return static_cast<bool>(left) && static_cast<bool>(right);
}

Condition operator|(const Condition &left, const Condition &right)
{
    return Condition{ left.value || right.value };
}

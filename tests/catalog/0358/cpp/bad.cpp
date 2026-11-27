struct Condition {
    bool value;
};

bool operator&&(const Condition &left, const Condition &right)
{
    return left.value && right.value;
}

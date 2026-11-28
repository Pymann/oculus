struct Error {
    explicit Error(int code) noexcept : code(code) {}
    int code;
};

void fail(int where)
{
    throw Error(where);
}

template <typename T>
int code(T) { return 0; }

template <>
int code<int>(int) { return 1; }

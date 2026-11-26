template <typename T>
int code(T) { return 0; }

inline int code(int) { return 1; }

template <typename T>
struct Code { static int get() { return 0; } };

template <>
struct Code<int> { static int get() { return 1; } };

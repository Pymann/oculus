template <typename T>
struct Size { static constexpr unsigned value = sizeof(T); };

template <>
struct Size<void> { static constexpr unsigned value = 0U; };

template struct Size<int>;

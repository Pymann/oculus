class Timeout {
public:
    Timeout(int milliseconds, bool repeat = false) : ms_(milliseconds), repeat_(repeat) {}
private:
    int ms_;
    bool repeat_;
};

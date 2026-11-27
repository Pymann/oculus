struct Point { int x; int y; };   /* aggregate: no invariant */

class Range {
public:
    static constexpr int none = -1;
    Range(int low, int high) : low_(low), high_(high) {}
    int low() const { return low_; }
    int high() const { return high_; }
private:
    int low_;
    int high_;
};

struct Sink {
    virtual ~Sink() = default;
    virtual void put(int value) = 0;
};

struct Null : Sink {
    void put(int) override {}
};

struct Counter : Sink {
    int total = 0;
    void put(int value) override { total += value; }   // value used in one override
};

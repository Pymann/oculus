struct Shape {
    virtual ~Shape() = default;
    virtual void draw() const {}
    void reset() {}
};

using action_t = void (Shape::*)() const;
using plain_t = void (Shape::*)();

bool has_action(action_t action) { return action != nullptr; }
bool is_reset(plain_t action) { return action == &Shape::reset; }

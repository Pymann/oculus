struct Shape {
    virtual ~Shape() = default;
    virtual void draw() const {}
    virtual void hide() const {}
};

using action_t = void (Shape::*)() const;

bool is_draw(action_t action)
{
    return action == &Shape::draw;
}

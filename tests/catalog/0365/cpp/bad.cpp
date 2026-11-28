struct Node { int value; };

int read_value()
{
    Node *node = new Node{ 7 };
    int value = node->value;
    delete node;
    return value;
}

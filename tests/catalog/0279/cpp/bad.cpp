struct Base { int value = 0; };
struct Left : virtual Base {};
struct Right : Base {};
struct Final : Left, Right {};

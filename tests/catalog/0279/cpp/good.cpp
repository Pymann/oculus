struct Base { int value = 0; };
struct Left : virtual Base {};
struct Right : virtual Base {};
struct Final : Left, Right {};

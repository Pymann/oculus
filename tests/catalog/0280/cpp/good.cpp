struct Reader { int size() const { return 1; } };
struct Writer { int size() const { return 2; } };
struct Stream : Reader, Writer {
    int size() const { return Reader::size() + Writer::size(); }
};

struct Base { int id = 0; };
struct Left : virtual Base {};
struct Right : virtual Base {};
struct Both : Left, Right {};   /* id comes from one shared base */

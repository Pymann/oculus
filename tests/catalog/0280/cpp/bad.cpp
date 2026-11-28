struct Reader { int size() const { return 1; } };
struct Writer { int size() const { return 2; } };
struct Stream : Reader, Writer {};

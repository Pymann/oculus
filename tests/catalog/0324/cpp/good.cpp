struct Header {
    int tag;
    int length;
};

int Header::*length_member = &Header::length;

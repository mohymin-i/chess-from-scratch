#include <vector>
#include <cmath>
#include <iostream>
#include<piece.cpp>

using std::cout;
using std::endl;
using std::vector;

const int a = 0;
const int b = 1;
const int c = 2;
const int d = 3;
const int e = 4;
const int f = 5;
const int g = 6;
const int h = 7;


class Board {
private:
    unint64_t emptyBoard = 0x0000000000000000; // for future reference
    // every two digits represents a rank in the board
    // 00FF000000000000 -> empty squares on the first rank
    // Second rank entirely filled
    // Fitting for pawns

    // We can hardcode bitboards since there will always be 12 for the pieces



public:
    Board() {

    } // default constructor

};
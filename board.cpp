#include <vector>
#include <cmath>
#include <iostream>
#include<piece.cpp>

using std::cout;
using std::endl;
using std::vector;



class Board {
private:
    unint64_t emptyBoard = 0x0000000000000000; // for future reference
    // every two digits represents a rank in the board
    // 00FF000000000000 -> empty squares on the first rank
    // Second rank entirely filled
    // Fitting for pawns

    // We can hardcode bitboards since there will always be 12 for the pieces
    // and several more for calculations

    uint64_t allPieces = 0xFFFF00000000FFFF; // All pieces - first and last 2 rows occupied
    uint64_t whitePieces = 0xFFFF000000000000;
    uint64_t blackPieces = 0xFFFF000000000000;
    uint64_t enPassant = 0x0000000000000000;



public:
    Board() {

    } // default constructor

};
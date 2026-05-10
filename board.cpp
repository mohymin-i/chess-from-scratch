#include <vector>
#include <cmath>
#include <iostream>
#include <piece.cpp>
#include <cstdint>

using std::cout;
using std::endl;
using std::vector;



class Board {
private:
    uint64_t emptyBoard = 0x0000000000000000; // for future reference
    // every two digits represents a rank in the board
    // 00FF000000000000 -> empty squares on the first rank
    // Second rank entirely filled
    // Fitting for pawns

    // We can hardcode bitboards since there will always be 12 for the pieces
    // and several more for calculations
    
    // All pieces - first and last 2 rows occupied
    uint64_t allPieces = 0xFFFF00000000FFFF; 
    uint64_t whitePieces = 0xFFFF000000000000;
    uint64_t blackPieces = 0xFFFF000000000000;
    uint64_t enPassant = 0x0000000000000000;

    // white pieces
    uint64_t whitePawns = 0x0000000000000000;
    uint64_t whiteRooks = 0x0000000000000000;
    uint64_t whiteKnights = 0x0000000000000000;
    uint64_t whiteBishops = 0x0000000000000000;
    uint64_t whiteQueens = 0x0000000000000000;
    uint64_t whiteKing = 0x0000000000000000; 

    // black pieces
    uint64_t blackPawns = 0x0000000000000000;
    uint64_t blackRooks = 0x0000000000000000;
    uint64_t blackKnights = 0x0000000000000000;
    uint64_t blackBishops = 0x0000000000000000;
    uint64_t blackQueens = 0x0000000000000000;
    uint64_t blackKing = 0x0000000000000000; 



public:
    Board() {

    } // default constructor


    void print() {

    } // print



    void checkmate() {

    } // checkmate


    bool movePawn(uint64_t base, uint64_t target) {
        // validate move

        // update
    }


    
};
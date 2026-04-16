#pragma once

class Board;

class Piece {

private:
    const int value;
    const bool white; // false if black
    int rank;
    int file;
    bool captured;


public:
    Piece () {}

    Piece(int rank, int file) {

    }


    Piece(int value, bool white, int rank, int file) {
        captured = false; // defualt to uncaptured
    }
};

// this class is probably doing to be the hardest to implement
class King : public Piece {
public:
    bool validMove() {
        return true;
    }

    bool move() {

    }
};

class Pawn : public Piece {
private:
    bool firstMove = true;
    bool enPassant = false;


public:
    bool move()
    }
}



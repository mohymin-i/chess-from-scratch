# Bitboards
_This file contains my notes for understanding and implementing bitboards_


## What are bitboards? 
- 64 bit representations of chess boards
- One bitboard for every color and piece type combination
- Each bit in the bitboard represents a single square on the board
- 0 means that there is no piece there
- 1 means that there is a piece there
- Makes it far easier to compute moves and stuff

## Starting Bitboards

The following is a table that contains the initial states of all bitboards implemented. 

| Piece Type   | Bitboard |
|--------------|----------|
| White Pawns  | temp     |
| White Rooks  | temp     |
| White Knight | temp     |
| White Bishop | temp     |
| White Queen  | temp     |
| White King   | temp0    |
| Black Pawns  | temp     |
| Black Rooks  | temp     |
| Black Knight | temp     |
| Black Bishop | temp     |
| Black Queen  | Temp     |
| Black King   | Temmp    |




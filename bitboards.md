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

## Square numbering

Squares use **little-endian rank-file** mapping, the convention most chess programming references use:
bit 0 is a1, bit 7 is h1, bit 8 is a2, ..., bit 63 is h8. In other words, `square = rank * 8 + file`
(both counted from 0). White starts on the low bits and black on the high bits.
In code, squares are the named constants `A1` … `H8` from `board.h`.

```
8 | 56 57 58 59 60 61 62 63
7 | 48 49 50 51 52 53 54 55
6 | 40 41 42 43 44 45 46 47
5 | 32 33 34 35 36 37 38 39
4 | 24 25 26 27 28 29 30 31
3 | 16 17 18 19 20 21 22 23
2 |  8  9 10 11 12 13 14 15
1 |  0  1  2  3  4  5  6  7
    a  b  c  d  e  f  g  h
```

## Starting Bitboards

The following is a table that contains the initial states of all bitboards implemented. 

| Piece Type   | Bitboard           |
|--------------|--------------------|
| White Pawns  | 0x000000000000FF00 |
| White Rooks  | 0x0000000000000081 |
| White Knight | 0x0000000000000042 |
| White Bishop | 0x0000000000000024 |
| White Queen  | 0x0000000000000008 |
| White King   | 0x0000000000000010 |
| Black Pawns  | 0x00FF000000000000 |
| Black Rooks  | 0x8100000000000000 |
| Black Knight | 0x4200000000000000 |
| Black Bishop | 0x2400000000000000 |
| Black Queen  | 0x0800000000000000 |
| Black King   | 0x1000000000000000 |

The queen and king rows are the ones that depend on the numbering: d1 is bit 3 (`0x08`) and e1 is bit 4 (`0x10`).





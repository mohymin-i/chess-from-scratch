# Chess from Scratch

Built entirely by Mohymin Islam, computer science student at the University of Michigan. 

## Progress

As of right now, this project is in its infancy. I am currently working on implementing the core game with two players first.

## Running the tests

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To also catch memory errors and undefined behavior, build a second copy with sanitizers:

```sh
cmake -S . -B build-asan -DCHESS_SANITIZE=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

To run a subset, pass part of a test name: `./build/chess_tests board_place`.

# Blackjack

## Build Status

Branch|Build Status
---|---
Master|[![master](https://travis-ci.org/DaoCasino/blackjack-game.svg?&branch=master)](https://travis-ci.org/DaoCasino/blackjack-game)
Develop|[![develop](https://travis-ci.org/DaoCasino/blackjack-game.svg?branch=develop)](https://travis-ci.org/DaoCasino/blackjack-game)

## Description
An implemenation of european blackjack with 8 decks. You can find the rules [here](https://www.blackjackclassroom.com/blackjack-games/european-blackjack).
RTP stands at 99.3 % for 1M+ rounds.

## Build
```bash
git clone https://github.com/DaoCasino/blackjack-game
cd blackjack-game
git submodule init
git submodule update --init --recursive
./cicd/run build
```
## Run unit tests
```bash
./cicd/run test
```

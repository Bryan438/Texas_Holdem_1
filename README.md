# Texas Hold’em Server 🂡🂮🂱

> **Author:** Bryan  
> **Language:** C++17  
> **Status:** 🛠 Alpha – playable, under refactor  
> **Repos:**  
> • Server (this repo)  
> • Client → https://github.com/Bryan438/TH_Client  

![GitHub last commit](https://img.shields.io/github/last-commit/Bryan438/Texas_Holdem_1?logo=github)
![Lines of code](https://img.shields.io/tokei/lines/github/Bryan438/Texas_Holdem_1)
![License](https://img.shields.io/github/license/Bryan438/Texas_Holdem_1)

---

## Project Goal
A lightweight, headless Texas Hold’em engine that **accepts socket connections from multiple CLI/GUI clients**, enforces game flow, and broadcasts game-state deltas.

---

## Architecture Overview
```mermaid
flowchart TD
  A[CLI / GUI clients] -- JSON/TCP --> B(Table Server)
  B -- "observer pattern" --> C[Controller]
  C --> D[Table]
  D --> E[Player<br/>objects]
  D --> F[DeckOfCard]
  C --> G["Transport<br/>send/recv"]

To Build Server
git clone https://github.com/Bryan438/Texas_Holdem_1.git
cd Texas_Holdem_1
make  
./th_server

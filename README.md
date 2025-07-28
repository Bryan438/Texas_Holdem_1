# Texas Hold’em Server 🂡🂮🂱

> **Author:** Bryan  
> **Language:** C++17  
> **Status:** 🛠 Alpha – playable, still under heavy refactor  
> **Repos:**  
> • Server (this repo)  
> • Client → https://github.com/Bryan438/TH_Client  

![GitHub last commit](https://img.shields.io/github/last-commit/Bryan438/Texas_Holdem_1?logo=github)
![Lines of code](https://img.shields.io/tokei/lines/github/Bryan438/Texas_Holdem_1)
![License](https://img.shields.io/github/license/Bryan438/Texas_Holdem_1)

---

## Table of Contents
1. [Project Goal](#project-goal)  
2. [Architecture Overview](#architecture-overview)  
3. [Build & Run](#build--run)  
4. [Wire Protocol](#wire-protocol)  
5. [Folder Structure](#folder-structure)  
6. [Testing](#testing)  
7. [Roadmap](#roadmap)  
8. [Contributing](#contributing)  
9. [License](#license)

---

## Project Goal
A lightweight, headless Texas Hold’em engine that **accepts socket connections from multiple CLI/GUI clients**, enforces game flow, and broadcasts game-state deltas.

---

```mermaid
flowchart TD
  A[CLI / GUI clients] -- JSON/TCP --> B(Table Server)
  B -- "observer pattern" --> C[Controller]
  C --> D[Table]
  D --> E[Player<br/>objects]
  D --> F[DeckOfCard]
  C --> G["Transport<br/>send/recv"]
```                   <!-- ← 1) close the Mermaid fence -->
                      <!-- ← 2) leave this line completely blank -->

## Build & Run 🏗️

### Server

```bash
git clone https://github.com/Bryan438/Texas_Holdem_1.git
cd Texas_Holdem_1
make                 # builds  ./th_server
./th_server -p 5000  # default port





# NF_Tx_Core

A C++ library for managing financial transactions and balances. This library provides functionality to handle wallet
data, transaction data, and calculate balances. It also includes features for working with crypto and card transactions.

## Features

- Initialization with log file path and load directory path
- Saving and loading data
- Setting wallet, card wallet, transaction, and card transaction data
- Calculating balances
- Retrieving information such as active modes, currencies, and financial metrics
- Resetting the library

## Usage

1. Include the library in your project.
2. Initialize the library using `init` or `initWithData` functions.
3. Set required data using corresponding setter functions.
4. Perform calculations using the provided methods.
5. Retrieve results using getter functions.

## Developed for

- Linux
- Android
- (Windows: works but not tested)

## License

Shield: [![CC BY-NC-ND 4.0][cc-by-nc-nd-shield]][cc-by-nc-nd]

This work is licensed under a
[Creative Commons Attribution Non Commercial No Derivatives 4.0 International License][cc-by-nc-nd].

[![CC BY-NC-ND 4.0][cc-by-nc-nd-image]][cc-by-nc-nd]

[cc-by-nc-nd]: http://creativecommons.org/licenses/by-nc-nd/4.0/

[cc-by-nc-nd-image]: https://licensebuttons.net/l/by-nc-nd/4.0/88x31.png

[cc-by-nc-nd-shield]: https://img.shields.io/badge/License-CC%20BY--NC--ND%204.0-lightgrey.svg

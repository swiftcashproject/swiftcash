# SwiftCash

`SwiftCash` (`SWIFT`) is an `ERC-20` compatible digital cash protocol deployed on the `BNB Smart Chain`.
The protocol combines an interest-bearing deposit system with decentralized, stake-weighted monetary governance.

## Features

- **`ERC-20` compatible token** — Standard `ERC-20` interface and `OpenZeppelin` implementation.
- **`ERC-20` Permit** — Supports `EIP-2612` off-chain approvals and gasless approval flows through relayers.
- **Transfer with data** — Supports `transfer(address,uint256,bytes)` for arbitrary data and off-chain metadata.
- **Interest-bearing deposits** — Users can deposit `SWIFT` into the protocol and earn interest.
- **`1%`–`10%` annual interest rate** — The protocol rate is constrained between `1%` and `10%`.
- **Stake-weighted rate adjustment** — Deposit holders can hike/cut the rate based on their stake.
- **Monthly rate adjustment** — Each eligible account may adjust the interest rate once per calendar month.
- **On-chain interest compounding** — Accrued interest is periodically minted directly to the deposit pool.
- **No predefined minimum deposit** — There is no fixed minimum deposit balance.
- **No administrator or owner controls** — The contract contains no owner or administrator privileges.

## Monetary Policy

The initial annual interest rate is **3%**.
The protocol defines:
- Minimum annual interest rate: **1%**
- Maximum annual interest rate: **10%**
- Maximum aggregate monthly rate movement: **1 percentage point**

An account's rate-adjustment power is proportional to its share of the total staking pool.
Each eligible account may make one rate adjustment per calendar month.

## Interest

Deposited `SWIFT` is represented internally through staking shares.
Interest is calculated across the entire staking pool, with each account receiving a proportional share based on its staking shares. Anyone can call `mintInterest()` to crystallize all currently accrued interest for the staking pool.

## Core Functions

### Deposits

- `deposit(uint256 amount)`
- `withdraw(uint256 amount)`
- `withdrawAll()`<br>

### Interest

- `currentAnnualInterestRate()`
- `accruedInterestOf(address account)`
- `mintInterest()`

### Monetary Governance

- `canAdjustInterestRate(address account)`
- `adjustmentPowerOf(address account)`
- `hikeInterest()`
- `cutInterest()`


### Staking Information

- `stakedBalanceOf(address account)`
- `accountStakingSharesOf(address account)`
- `totalStakedSharesCount()`
- `lastStakingDepositMonthOf(address account)`
- `lastRateAdjustmentMonthOf(address account)`

### Token Functions

- `transfer(address to, uint256 amount, bytes data)`
- `virtualTotalSupply()`

Standard `ERC-20`, `ERC-20Permit` and `ERC20Burnable` functions inherited from `OpenZeppelin` are also available.

## Smart Contract

`SwiftCash` is implemented in `Solidity` using OpenZeppelin's implementations.

### Compiler

`Solidity ^0.8.34`

### Dependencies

* `OpenZeppelin` Contracts

### Initial Supply

- `~315,000,000 SWIFT` 1:1 claims by legacy private keys via the Migration contract. 
- `~15,000,000 SWIFT` For setting up permanent locked liquidity pools.

### Mainnet Contract on the BNB Smart Chain

[TBA]

## Development

The contract can be compiled and tested using [Remix](https://remix.ethereum.org/) or other standard Solidity development environments. The repository contains the source code and supporting documentation for the SwiftCash protocol.

## License

MIT


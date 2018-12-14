SwiftCash Core integration/staging repository
=================================================
[![Build Status](https://travis-ci.org/swiftcashproject/swiftcash.svg?branch=master)](https://travis-ci.org/swiftcashproject/swiftcash) ![GitHub](https://img.shields.io/github/license/mashape/apistatus.svg)

SwiftCash is the SplitFork of SmartCash as of block 633,000 - appx. 1st of September 2018. Driven by strong divisions between the community and the core team(smarthives), about 80% of the total supply which was believed to be mostly controlled or owned by the core team(smarthives) or exchanges or hackers was burned which then turned into a unique experience with an amazing initial decentralization of power, supply ownership, as well as inflation. SwiftCash (SWIFT) is a decentralized, peer-to-peer transactional currency which also offers a solution to the problem posed by the exponential increase in energy consumed by Bitcoin and other proof-of-work currencies. Proof-of-work mining is environmentally unsustainable due to the electricity used by high-powered mining hardware and anyone with 51% hash power can control the network and double spend. SwiftCash utilizes the Green Protocol, an energy efficient proof-of-stake algorithm inspired by Bitcoin Green, can be mined on any computer, and will never require specialized mining equipment. The Green Protocol offers a simple solution to Bitcoin sustainability issues and provides a faster, more scalable blockchain that is better suited for daily transactional use.

- Fast transactions featuring instant locks on zero confirmation transactions, we call it _SwiftTX_.
- Decentralized blockchain voting providing for consensus based advancement of the current SwiftNode
  technology used to secure the network and provide the above features, each SwiftNode is secured
  with a collateral of 20,000 SWIFT.

More information at [swiftcash.cc](http://www.swiftcash.cc) or [swiftcash.org](http://www.swiftcash.org)

Please reach out at info@swiftcash.cc or info@swiftcash.org

### Coin Specs
| Block Time                  | ~1 minute             |
| ForkDrops Phase (PoW Phase) | ~299,000,000 SWIFT    |
| Max Coin Supply (PoS Phase) | ~4,700,000,000 SWIFT  |
| Initial Development         | ~1,000,000 SWIFT      |

### Reward Distribution

| **Block Height** | **SwiftNodes**     | **PoS Miners**     |
|------------------|--------------------|--------------------|
| 201-10000        | 20% (~10 SWIFT)     | 10% (~5 SWIFT)     | 
| 10001-Infinite   | 20% (~100-0 SWIFT) | 10% (~50-0 SWIFT)  |
|------------------|--------------------|--------------------|

### Minimum & Maximum Block Rewards

Community proposals will be allowed to use 70% of the block rewards for budgetting as calculated with the following formula. 10% of the budget should be used for SwiftRewards which will later be coded into the blockchain. Any amount that is not used can be mined in the future for budgetting or other purposes such as mining or staking. SwiftCash block rewards start with a minimum of 150 SWIFT per block after block 10,000, and slowly curve towards 0. It takes 8 years for block rewards to slowly halve for the first time. The second halving will take 16 years, third halving 32 years and so on, until maximum supply of 5,000,000,000 SWIFT is reached.

Maximum Block Rewards = (0.5 + 4000 * 525600) / (8*525600 + nHeight - 10000 + 1)

Minimum Block Rewards = (0.5 + 1200 * 525600) / (8*525600 + nHeight - 10000 + 1)

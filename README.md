SwiftCash Core integration/staging repository
=================================================
![GitHub](https://img.shields.io/github/license/mashape/apistatus.svg) [![Build Status](https://travis-ci.org/swiftcashproject/swiftcash.svg?branch=master)](https://travis-ci.org/swiftcashproject/swiftcash) [![Discord](https://img.shields.io/discord/488436992032636929.svg)](https://discord.me/swiftcash)

SwiftCash is an open-source, decentralized, peer-to-peer transactional cryptocurrency and economy which also offers a solution to the problem posed by the exponential increase in energy consumed by Bitcoin, and other Proof-of-Work cryptocurrencies. Proof-of-Work mining is environmentally unsustainable due to the electricity used by high-powered mining hardware and anyone with 51% hash power can control the network and double spend. SwiftCash utilizes the Green Protocol, an energy efficient Proof-of-Stake algorithm inspired by Bitcoin Green, can be mined on any computer, and will never require specialized mining equipment.

The Green Protocol offers a simple solution to Bitcoin sustainability issues and provides a faster, more scalable blockchain that is better suited for daily transactional use. As a decentralized economy, SwiftCash uses onchain governance to reach consensus about the direction and scope of development, support and outreach activities. Higher stake will have higher voting power and so this way, all key decisions will ultimately be made by active stakeholders, as we believe it should.

- Fast transactions featuring instant locks on zero confirmation transactions, we call it _SwiftTX_.
- Decentralized blockchain voting providing for consensus based advancement of the current SwiftNode
  technology used to secure the network and provide the above features, each SwiftNode is secured
  with a collateral of 50,000 SWIFT.

More information at [swiftcash.cc](http://www.swiftcash.cc) or [swiftcash.org](http://www.swiftcash.org)

Please reach out at info@swiftcash.cc or info@swiftcash.org

### Coin Specs
|                                  |                       |
|----------------------------------|-----------------------|
| Block Time                       | ~1 minute             |
| Algorithm                        | Keccak                |
| Difficulty Adjustment (Timespan) | 40 blocks             |
| Difficulty Adjustment (Interval) | Every block           |
| ForkDrops Phase (PoW Phase)      | ~80,000,000 SWIFT     |
| Max Coin Supply (PoS Phase)      | ~4,920,000,000 SWIFT  |
| Maximum Supply (Total)           | 5,000,000,000 SWIFT   |
| Proposal Fee                     | 100 SWIFT             |
| Budget Fee                       | 10 SWIFT              |

### Block Rewards Distribution

| **Block Height** | **SwiftNodes**     | **PoS Miners**     |
|------------------|--------------------|--------------------|
| 201-10000        | 20% (~10 SWIFT)    | 10% (~5 SWIFT)     | 
| 10001-Infinite   | 20% (~40-0 SWIFT)  | 10% (~20-0 SWIFT)  |

### Minimum & Maximum Block Rewards

Community proposals will be allowed to use 70% of the block rewards for budgetting as calculated with the following formula. 10% of the budget should be used for SwiftRewards which will later be coded into the blockchain. Any amount that is not used can be mined in the future for budgetting or other purposes such as mining or staking. SwiftCash block rewards start with a minimum of 60 SWIFT per block after block 10,000, and slowly curve towards 0. It takes 20 years for block rewards to slowly halve for the first time. The second halving will take another 40 years, third halving, another 80 years and so on, until maximum supply of 5,000,000,000 SWIFT is reached.

Maximum Block Rewards = (20 * 200 * 525600) / (20 * 525600 + nHeight - 10000)

Minimum Block Rewards = (20 * 60 * 525600) / (20 * 525600 + nHeight - 10000)

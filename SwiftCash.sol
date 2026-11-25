// SPDX-License-Identifier: MIT
pragma solidity ^0.8.39;

import {ERC20} from "@openzeppelin/contracts/token/ERC20/ERC20.sol";
import {ERC20Permit} from "@openzeppelin/contracts/token/ERC20/extensions/ERC20Permit.sol";
import {ERC20Burnable} from "@openzeppelin/contracts/token/ERC20/extensions/ERC20Burnable.sol";


contract SwiftCash is ERC20, ERC20Permit, ERC20Burnable {

    // ============================================================
    // SWIFTCASH — DECENTRALIZED DIGITAL CASH
    // ============================================================


    // ============================================================
    // MONETARY POLICY
    // Decentralized Stake-Weighted Interest-Rate Adjustment
    // Minimum 1% and Maximum 10% Annual Rates
    // ============================================================

    /*
     * Fixed-point precision.
     *
     * RATE_SCALE = 1e27
     *
     * This gives the interest-rate calculation 27 decimal
     * places of internal precision.
     */
    uint256 private constant RATE_SCALE = 1e27;

    /*
     * 365 × 24 × 60 = 525,600 minutes per year.
     */
    uint256 private constant MINUTES_PER_YEAR = 525600;

    /*
     * Minimum annual interest rate:
     *
     * 1%
     */
    uint256 private constant MIN_ANNUAL_RATE = 1e25;

    /*
     * Maximum annual interest rate:
     *
     * 10%
     */
    uint256 private constant MAX_ANNUAL_RATE = 1e26;

    /*
     * Maximum aggregate rate movement:
     *
     * 1 percentage point.
     *
     * If one address controlled 100% of the staking pool,
     * its maximum adjustment would therefore be 1 percentage
     * point.
     */
    uint256 private constant MAX_MONTHLY_CHANGE = 1e25;

    /*
     * Current annual interest rate.
     *
     * Initial rate:
     *
     * 3%
     */
    uint256 private currentAnnualRate = 3e25;


    // ============================================================
    // STAKING
    // ============================================================

    /*
     * Internal staking shares.
     *
     * These are NOT ERC-20 tokens.
     */
    mapping(address => uint256) private accountStakingShares;

    uint256 private totalStakedShares;

    /*
     * Calendar month in which the address most recently
     * deposited.
     *
     * Additional deposits reset the staking-age clock
     * for the entire address.
     */
    mapping(address => uint256) private lastStakingDepositMonth;

    /*
     * Calendar month in which the address most recently
     * changed the interest rate.
     *
     * Each address may make only one interest-rate adjustment
     * during a calendar month.
     */
    mapping(address => uint256) private lastRateAdjustmentMonth;

    /*
     * Last time the staking pool was actually compounded.
     */
    uint256 private lastInterestMintTimestamp;

    // ============================================================
    // CUSTOM ERRORS
    // ============================================================

    error AmountMustBeGreaterThanZero();
    error InvalidStakingPool();
    error AmountTooSmall();
    error NoStakingPosition();
    error InsufficientStakingBalance();
    error InsufficientStakedBalance();
    error NoStakingShares();
    error NotEligibleToAdjustInterestRate();
    error MaximumRateReached();
    error MinimumRateReached();


    // ============================================================
    // CUSTOM EVENTS
    // ============================================================

    /*
     * Emitted when tokens are transferred with additional data.
     */
    event TransferWithData(
        address indexed from,
        address indexed to,
        uint256 value,
        bytes data
    );

    /*
     * Emitted when an account changes the interest rate.
     */
    event InterestRateChanged(
        address indexed account,
        uint256 previousRate,
        uint256 newRate
    );


    // ============================================================
    // CONSTRUCTOR
    // ============================================================
    
    constructor() payable
        ERC20("SwiftCash", "SWIFT")
        ERC20Permit("SwiftCash")
    {
        /*
         * Initial supply:
         *
         * ~330,000,000 SWIFT:
         *
         * ~282M SWIFT for legacy UTXOs
         * ~32M SWIFT for the deprecated wrapper (wSWIFT)
         * ~16M SWIFT for protocol-level liquidity pools
         *
         * Legacy UTXOs and wSWIFT tokens will be claimable
         * via a non-custodial and ownerless contract
         *
         * Protocol-level liquidity pools will eventually be
         * established in a non-custodial and ownerless contract
         */
        _mint(
            msg.sender,
            330_000_000 * 10 ** 18
        );

        lastInterestMintTimestamp = block.timestamp;
    }


    // ============================================================
    // TRANSFER WITH DATA
    // ============================================================

    /*
     * Transfers tokens with optional arbitrary data.
     *
     * This is an overloaded version of the standard ERC-20
     * transfer() function.
     *
     * The data is emitted as an event and is NOT stored in
     * the contract's state.
     *
     * This can be used for off-chain metadata and potential
     * future integrations such as decentralized bridges.
     */
    function transfer(
        address to,
        uint256 value,
        bytes calldata data
    )
        public
        returns (bool)
    {
        _transfer(
            _msgSender(),
            to,
            value
        );

        emit TransferWithData(
            _msgSender(),
            to,
            value,
            data
        );

        return true;
    }


    // ============================================================
    // DEPOSIT
    // ============================================================

    /*
    * Deposit SWIFT into the interest-bearing staking pool.
    *
    * The deposited tokens remain under the control of the
    * SwiftCash smart contract and are represented internally
    * by staking shares.
    *
    * Interest is calculated across the entire staking pool,
    * with each account receiving a proportional share based
    * on its staking shares.
    *
    * Additional deposits reset the account's staking-age clock
    * for interest-rate adjustment eligibility.
    */
    function deposit(uint256 amount) external {

        if (amount == 0) {
            revert AmountMustBeGreaterThanZero();
        }

        /*
         * Crystallize all interest accumulated before
         * changing the staking pool composition.
         */
        _mintInterest();

        uint256 poolBalance =
            balanceOf(address(this));

        uint256 shares;

        if (totalStakedShares == 0) {

            shares = amount;

        } else {

            /*
             * Explicitly ensure the staking pool has a
             * non-zero balance before performing the division.
             */
            if (poolBalance == 0) {
                revert InvalidStakingPool();
            }

            shares =
                (amount * totalStakedShares) /
                poolBalance;
        }

        if (shares == 0) {
            revert AmountTooSmall();
        }

        /*
        * There is intentionally NO minimum deposit balance.
        *
        * Even a microscopic deposit can participate in
        * monetary-policy governance.
        *
        * Its influence is proportional to its share
        * of the total staking pool.
        */

        _transfer(
            msg.sender,
            address(this),
            amount
        );

        accountStakingShares[msg.sender] = accountStakingShares[msg.sender] + shares;

        totalStakedShares = totalStakedShares + shares;

        /*
         * Reset the staking-age clock whenever the address
         * makes an additional deposit.
         */
        lastStakingDepositMonth[msg.sender] =
            _currentMonth();

    }


    // ============================================================
    // WITHDRAW
    // ============================================================

    /*
    * Withdraw SWIFT from the interest-bearing staking pool.
    *
    * The withdrawal amount is converted into the corresponding
    * number of staking shares and those shares are removed
    * from the account's staking position.
    *
    * The user receives the underlying SWIFT represented by
    * those shares, including any interest that has already
    * been minted into the staking pool.
    */
    function withdraw(uint256 amount) external {

        if (amount == 0) {
            revert AmountMustBeGreaterThanZero();
        }

        _mintInterest();

        uint256 poolBalance =
            balanceOf(address(this));

        uint256 accountShares =
            accountStakingShares[msg.sender];

        if (accountShares == 0) {
            revert NoStakingPosition();
        }

        if (amount > poolBalance) {
            revert InsufficientStakingBalance();
        }

        /*
         * Round UP so the user cannot withdraw more than
         * the value represented by their shares.
         */
        uint256 sharesToBurn =
            (
                amount * totalStakedShares +
                poolBalance -
                1
            ) / poolBalance;

        if (sharesToBurn > accountShares) {
            revert InsufficientStakedBalance();
        }

        accountStakingShares[msg.sender] -= sharesToBurn;

        totalStakedShares -= sharesToBurn;

        _transfer(
            address(this),
            msg.sender,
            amount
        );

    }


    // ============================================================
    // WITHDRAW ALL
    // ============================================================

    /*
    * Withdraw all SWIFT from the interest-bearing staking pool.
    *
    * The caller's entire staking position is converted into
    * its current underlying SWIFT value, including any interest
    * that has just been minted into the staking pool.
    *
    * The account's staking shares are then zeroed atomically.
    */
    function withdrawAll() external {

        _mintInterest();

        uint256 poolBalance =
            balanceOf(address(this));

        uint256 accountShares =
            accountStakingShares[msg.sender];

        if (accountShares == 0) {
            revert NoStakingPosition();
        }
        
        if (totalStakedShares == 0) {
            revert NoStakingShares();
        }

        /*
         * Calculate the full SWIFT value represented by
         * the account's staking shares.
         */
        uint256 amount =
            (
                accountShares * poolBalance
            ) / totalStakedShares;

        if (amount == 0) {
            revert AmountMustBeGreaterThanZero();
        }

        totalStakedShares -= accountShares;

        accountStakingShares[msg.sender] = 0;

        _transfer(
            address(this),
            msg.sender,
            amount
        );

    }


    // ============================================================
    // STAKED BALANCE
    // ============================================================

    /*
     * Returns the user's CURRENT ACTUAL staking balance.
     *
     * Importantly, this does NOT include unminted interest.
     */
    function stakedBalanceOf(address account)
        public
        view
        returns (uint256)
    {
        uint256 shares =
            accountStakingShares[account];

        if (
            shares == 0 ||
            totalStakedShares == 0
        ) {
            return 0;
        }

        uint256 poolBalance =
            balanceOf(address(this));

        if (poolBalance == 0) {
            return 0;
        }

        /*
         * Round UP to prevent the displayed balance from
         * showing a tiny loss caused solely by integer division.
         *
         * This is a view-only calculation and does not change
         * the user's actual staking shares or token balance.
         */
        return
            (
                shares * poolBalance +
                totalStakedShares -
                1
            ) /
            totalStakedShares;
    }


    // ============================================================
    // ACCRUED INTEREST
    // ============================================================

    /*
     * Returns the user's currently accrued but UNMINTED interest.
     *
     * This is view-only.
     *
     * It does not mint tokens or modify state.
     */
    function accruedInterestOf(address account)
        public
        view
        returns (uint256)
    {
        uint256 shares =
            accountStakingShares[account];

        if (
            shares == 0 ||
            totalStakedShares == 0
        ) {
            return 0;
        }

        uint256 poolBalance =
            balanceOf(address(this));

        if (poolBalance == 0) {
            return 0;
        }

        uint256 currentBalance =
            (shares * poolBalance) /
            totalStakedShares;

        /*
         * Calculate the theoretical pool balance after applying
         * all currently accrued but unminted interest.
         *
         * No tokens are minted and no state is changed.
         */
        (
            uint256 poolInterest
            ,
        ) = _calculateAccruedPoolInterest();

        uint256 theoreticalPoolBalance =
            poolBalance + poolInterest;

        uint256 theoreticalBalance =
            (shares * theoreticalPoolBalance) /
            totalStakedShares;

        if (
            theoreticalBalance <= currentBalance
        ) {
            return 0;
        }

        return
            theoreticalBalance -
            currentBalance;
    }


    // ============================================================
    // VIRTUAL TOTAL SUPPLY
    // ============================================================

    /*
     * Returns the virtual total supply of SWIFT.
     *
     * The standard ERC-20 totalSupply() represents only tokens
     * that have actually been minted.
     *
     * virtualTotalSupply() additionally includes all interest
     * that has currently accrued for the staking pool but has
     * not yet been minted.
     *
     * This is a view-only calculation.
     *
     * It does NOT mint tokens or modify contract state.
     */
    function virtualTotalSupply()
        public
        view
        returns (uint256)
    {
        (
            uint256 poolInterest
            ,
        ) = _calculateAccruedPoolInterest();

        return
            totalSupply() +
            poolInterest;
    }


    // ============================================================
    // CURRENT INTEREST RATE
    // ============================================================

    /*
     * Returns the current annual interest rate using RATE_SCALE.
     *
     * Examples:
     *
     * 1%  = 1e25
     * 3%  = 3e25
     * 10% = 1e26
     */
    function currentAnnualInterestRate()
        external
        view
        returns (uint256)
    {
        return currentAnnualRate;
    }


    // ============================================================
    // STAKING INFORMATION
    // ============================================================

    function accountStakingSharesOf(address account)
        external
        view
        returns (uint256)
    {
        return accountStakingShares[account];
    }

    function totalStakedSharesCount()
        external
        view
        returns (uint256)
    {
        return totalStakedShares;
    }

    function lastStakingDepositMonthOf(address account)
        external
        view
        returns (uint256)
    {
        return lastStakingDepositMonth[account];
    }

    function lastRateAdjustmentMonthOf(address account)
        external
        view
        returns (uint256)
    {
        return lastRateAdjustmentMonth[account];
    }


    // ============================================================
    // RATE ADJUSTMENT ELIGIBILITY
    // ============================================================

    /*
     * Returns whether an address is currently eligible to
     * adjust the interest rate.
     *
     * An address must:
     *
     * 1. Currently have a staking position.
     *
     * 2. Have deposited before the current calendar month.
     *
     * 3. Not have already adjusted the interest rate during
     *    the current calendar month.
     *
     * This function is view-only and can therefore be called
     * by the frontend without paying gas.
     */
    function canAdjustInterestRate(address account)
        public
        view
        returns (bool)
    {
        uint256 shares =
            accountStakingShares[account];

        if (shares == 0) {
            return false;
        }

        uint256 currentMonth =
            _currentMonth();

        if (
            lastStakingDepositMonth[account] >=
            currentMonth
        ) {
            return false;
        }

        if (
            lastRateAdjustmentMonth[account] ==
            currentMonth
        ) {
            return false;
        }

        return true;
    }


    // ============================================================
    // ADJUSTMENT POWER
    // ============================================================

    /*
     * Returns the amount by which this address can move the
     * annual interest rate with one adjustment.
     *
     * Formula:
     *
     *     MAX_MONTHLY_CHANGE ×
     *     user's staking shares /
     *     total staking shares
     *
     * MAX_MONTHLY_CHANGE = 1 percentage point.
     *
     * Therefore:
     *
     * 100% of staking pool -> 1.00 percentage point
     *  50% of staking pool -> 0.50 percentage point
     *  10% of staking pool -> 0.10 percentage point
     *
     * The returned value uses RATE_SCALE.
     *
     * Example:
     *
     * 0.10 percentage point = 1e24
     */
    function adjustmentPowerOf(address account)
        public
        view
        returns (uint256)
    {
        uint256 shares =
            accountStakingShares[account];

        if (
            shares == 0 ||
            totalStakedShares == 0
        ) {
            return 0;
        }

        return
            (
                MAX_MONTHLY_CHANGE *
                shares
            ) /
            totalStakedShares;
    }


    // ============================================================
    // HIKE INTEREST
    // ============================================================

    /*
     * Increase the interest rate.
     *
     * The amount of the increase is proportional to the
     * caller's share of the staking pool.
     *
     * Eligibility is determined by canAdjustInterestRate().
     */
    function hikeInterest() external {

        if (!canAdjustInterestRate(msg.sender)) {
            revert NotEligibleToAdjustInterestRate();
        }

        /*
         * Interest is first crystallized using the OLD rate.
         *
         * Only after that is the new rate applied.
         */
        _mintInterest();

        /*
         * Get the caller's stake-weighted adjustment power.
         */
        uint256 adjustment =
            adjustmentPowerOf(msg.sender);

        /*
         * There is deliberately NO minimum adjustment
         * requirement.
         *
         * A microscopic deposit is allowed to participate.
         * If the mathematical result rounds to zero, the
         * transaction simply produces no rate change.
         */

        if (currentAnnualRate >= MAX_ANNUAL_RATE) {
            revert MaximumRateReached();
        }

        uint256 previousRate =
            currentAnnualRate;

        currentAnnualRate =
            currentAnnualRate + adjustment >
            MAX_ANNUAL_RATE
                ? MAX_ANNUAL_RATE
                : currentAnnualRate + adjustment;

        lastRateAdjustmentMonth[msg.sender] =
            _currentMonth();

        emit InterestRateChanged(
            msg.sender,
            previousRate,
            currentAnnualRate
        );
    }


    // ============================================================
    // CUT INTEREST
    // ============================================================

    /*
     * Decrease the interest rate.
     *
     * The amount of the decrease is proportional to the
     * caller's share of the staking pool.
     *
     * Eligibility is determined by canAdjustInterestRate().
     */
    function cutInterest() external {

        if (!canAdjustInterestRate(msg.sender)) {
            revert NotEligibleToAdjustInterestRate();
        }

        /*
         * Crystallize interest using the OLD rate first.
         */
        _mintInterest();

        /*
         * Get the caller's stake-weighted adjustment power.
         */
        uint256 adjustment =
            adjustmentPowerOf(msg.sender);

        /*
         * No minimum adjustment requirement.
         */

        if (currentAnnualRate <= MIN_ANNUAL_RATE) {
            revert MinimumRateReached();
        }

        uint256 previousRate =
            currentAnnualRate;

        currentAnnualRate =
            currentAnnualRate - adjustment <
            MIN_ANNUAL_RATE
                ? MIN_ANNUAL_RATE
                : currentAnnualRate - adjustment;

        lastRateAdjustmentMonth[msg.sender] =
            _currentMonth();

        emit InterestRateChanged(
            msg.sender,
            previousRate,
            currentAnnualRate
        );
    }


    // ============================================================
    // MINT / COMPOUND INTEREST
    // ============================================================

    /*
     * Anyone can call this.
     *
     * It crystallizes all currently accrued interest
     * for the staking pool.
     */
    function mintInterest() external {
        _mintInterest();
    }


    /*
     * Calculates the staking pool's currently accrued but
     * UNMINTED interest.
     *
     * This function is shared by:
     *
     * 1. _mintInterest()
     * 2. accruedInterestOf()
     * 3. virtualTotalSupply()
     *
     * It returns:
     *
     * - The current actual staking pool balance
     * - The currently accrued but unminted pool interest
     * - The number of complete minutes that have elapsed
     *
     * This is a view-only calculation and does NOT mint tokens
     * or modify contract state.
     */
    function _calculateAccruedPoolInterest()
        internal
        view
        returns (
            uint256 interest,
            uint256 minutesElapsed
        )
    {
        uint256 poolBalance =
            balanceOf(address(this));

        uint256 elapsed =
            block.timestamp -
            lastInterestMintTimestamp;

        /*
         * Only complete minutes compound.
         */
        minutesElapsed =
            elapsed / 1 minutes;

        if (
            minutesElapsed == 0 ||
            poolBalance == 0
        ) {
            return (
                0,
                minutesElapsed
            );
        }

        uint256 minuteRate =
            _currentMinuteRate();

        uint256 growthFactor =
            _rpow(
                minuteRate,
                minutesElapsed,
                RATE_SCALE
            );

        uint256 newBalance =
            (poolBalance * growthFactor) /
            RATE_SCALE;

        interest =
            newBalance - poolBalance;
    }


    function _mintInterest() internal {

        (
            uint256 interest,
            uint256 minutesElapsed
        ) = _calculateAccruedPoolInterest();

        if (interest > 0) {

            _mint(
                address(this),
                interest
            );

        }

        /*
         * Preserve any partial minute.
         */
        lastInterestMintTimestamp = lastInterestMintTimestamp +
            minutesElapsed * 1 minutes;
    }


    // ============================================================
    // CURRENT MINUTE RATE
    // ============================================================

    /*
     * Converts the current annual rate into a minute rate.
     *
     * Example:
     *
     * 3% annual
     * -> approximately 3% / 525,600 per minute.
     */
    function _currentMinuteRate()
        internal
        view
        returns (uint256)
    {
        uint256 minuteInterest =
            currentAnnualRate /
            MINUTES_PER_YEAR;

        return
            RATE_SCALE +
            minuteInterest;
    }


    // ============================================================
    // CALENDAR MONTH
    // ============================================================

    /*
     * Returns:
     *
     * year × 12 + month
     *
     * This is used for calendar-month policy rules.
     *
     * Calendar months are based on the Unix timestamp,
     * which corresponds to UTC.
     */
    function _currentMonth()
        internal
        view
        returns (uint256)
    {
        (
            uint256 year,
            uint256 month,
            ,
            ,
            ,
        ) = _timestampToDate(block.timestamp);

        return year * 12 + month;
    }


    // ============================================================
    // TIMESTAMP → CALENDAR DATE
    // ============================================================

    /*
     * Gregorian calendar conversion.
     *
     * Used only to determine the current calendar month.
     */
    function _timestampToDate(uint256 timestamp)
        internal
        pure
        returns (
            uint256 year,
            uint256 month,
            uint256 day,
            uint256 hour,
            uint256 minute,
            uint256 second
        )
    {
        uint256 daysSince1970 =
            timestamp / 86400;

        uint256 secondsToday =
            timestamp % 86400;

        hour =
            secondsToday / 3600;

        secondsToday %= 3600;

        minute =
            secondsToday / 60;

        second =
            secondsToday % 60;

        int256 z =
            int256(daysSince1970) +
            719468;

        int256 era =
            z >= 0
                ? z / 146097
                : (z - 146096) / 146097;

        uint256 doe =
            uint256(
                z -
                era * 146097
            );

        uint256 yoe =
            (
                doe -
                doe / 1460 +
                doe / 36524 -
                doe / 146096
            ) / 365;

        year =
            uint256(
                int256(yoe) +
                era * 400
            );

        uint256 doy =
            doe -
            (
                365 * yoe +
                yoe / 4 -
                yoe / 100
            );

        uint256 mp =
            (5 * doy + 2) / 153;

        day =
            doy -
            (153 * mp + 2) / 5 +
            1;

        if (mp < 10) {
            month = mp + 3;
        } else {
            month = mp - 9;
        }

        year +=
            month <= 2
                ? 1
                : 0;
    }


    // ============================================================
    // FIXED-POINT EXPONENTIATION
    // ============================================================

    function _rpow(
        uint256 x,
        uint256 n,
        uint256 scalar
    )
        internal
        pure
        returns (uint256 result)
    {
        result = scalar;

        while (n > 0) {

            if (n % 2 == 1) {

                result =
                    (result * x) /
                    scalar;
            }

            x =
                (x * x) /
                scalar;

            n /= 2;
        }
    }
}
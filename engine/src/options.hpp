// options_pricing.hpp
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <random>
#include <Eigen/Dense>
#include <boost/math/distributions/normal.hpp>

namespace sim {

/**
 * @brief Option type enumeration
 */
enum class OptionType {
    CALL,
    PUT
};

/**
 * @brief Exercise style enumeration
 */
enum class ExerciseStyle {
    EUROPEAN,  // Can only be exercised at expiration
    AMERICAN,  // Can be exercised at any time before expiration
    BERMUDAN   // Can be exercised on specific dates
};

/**
 * @brief Options contract specification
 */
struct OptionSpec {
    std::string underlying_id;   // ID of the underlying instrument
    OptionType type;             // Call or Put
    ExerciseStyle exercise;      // Exercise style
    double strike;               // Strike price
    double expiry_days;          // Days to expiration
    double lots;                 // Contract size in lots
    double multiplier;           // Value multiplier (e.g., 100 for standard equity options)
};

/**
 * @brief Greeks - option risk measures
 */
struct Greeks {
    double delta;  // First derivative with respect to underlying price
    double gamma;  // Second derivative with respect to underlying price
    double theta;  // First derivative with respect to time
    double vega;   // First derivative with respect to volatility
    double rho;    // First derivative with respect to interest rate
};

/**
 * @brief Base class for option pricing models
 */
class OptionPricingModel {
public:
    virtual ~OptionPricingModel() = default;
    
    // Calculate option price
    virtual double calculate_price(
        const OptionSpec& option,
        double underlying_price,
        double volatility,
        double risk_free_rate
    ) const = 0;
    
    // Calculate option Greeks
    virtual Greeks calculate_greeks(
        const OptionSpec& option,
        double underlying_price,
        double volatility,
        double risk_free_rate
    ) const = 0;
    
    // Calculate implied volatility from market price
    virtual double calculate_implied_volatility(
        const OptionSpec& option,
        double underlying_price,
        double market_price,
        double risk_free_rate
    ) const;

protected:
    // Common utility methods for option pricing
    double normal_cdf(double x) const;
    double normal_pdf(double x) const;
};

/**
 * @brief Black-Scholes model for European options
 */
class BlackScholesModel : public OptionPricingModel {
public:
    double calculate_price(
        const OptionSpec& option,
        double underlying_price,
        double volatility,
        double risk_free_rate
    ) const override;
    
    Greeks calculate_greeks(
        const OptionSpec& option,
        double underlying_price,
        double volatility,
        double risk_free_rate
    ) const override;
    
private:
    // Black-Scholes specific calculations
    double calculate_d1(double S, double K, double r, double sigma, double T) const;
    double calculate_d2(double d1, double sigma, double T) const;
};

/**
 * @brief Binomial tree model for American options
 */
class BinomialTreeModel : public OptionPricingModel {
public:
    BinomialTreeModel(int num_steps = 100);
    
    double calculate_price(
        const OptionSpec& option,
        double underlying_price,
        double volatility,
        double risk_free_rate
    ) const override;
    
    Greeks calculate_greeks(
        const OptionSpec& option,
        double underlying_price,
        double volatility,
        double risk_free_rate
    ) const override;
    
private:
    int num_steps_;
};

/**
 * @brief Monte Carlo model for complex options
 */
class MonteCarloModel : public OptionPricingModel {
public:
    MonteCarloModel(int num_simulations = 10000, int num_steps = 100);
    
    double calculate_price(
        const OptionSpec& option,
        double underlying_price,
        double volatility,
        double risk_free_rate
    ) const override;
    
    Greeks calculate_greeks(
        const OptionSpec& option,
        double underlying_price,
        double volatility,
        double risk_free_rate
    ) const override;
    
private:
    int num_simulations_;
    int num_steps_;
    mutable std::mt19937 rng_;
};

/**
 * @brief Heston stochastic volatility model
 */
class HestonModel : public OptionPricingModel {
public:
    HestonModel(double kappa = 2.0, double theta = 0.04, double sigma = 0.3, double rho = -0.7);
    
    double calculate_price(
        const OptionSpec& option,
        double underlying_price,
        double volatility,
        double risk_free_rate
    ) const override;
    
    Greeks calculate_greeks(
        const OptionSpec& option,
        double underlying_price,
        double volatility,
        double risk_free_rate
    ) const override;
    
private:
    // Heston model parameters
    double kappa_;  // Mean reversion speed
    double theta_;  // Long-term variance mean
    double sigma_;  // Volatility of volatility
    double rho_;    // Correlation between asset returns and volatility
};

/**
 * @brief Volatility surface for realistic option pricing
 */
class VolatilitySurface {
public:
    // Initialize with market data points
    VolatilitySurface(const std::vector<double>& strikes, 
                     const std::vector<double>& expiries,
                     const Eigen::MatrixXd& implied_vols);
    
    // Get implied volatility for specific strike and expiry
    double get_implied_volatility(double strike, double expiry) const;
    
    // Update the surface with new market data
    void update(const std::vector<double>& strikes,
               const std::vector<double>& expiries,
               const Eigen::MatrixXd& implied_vols);
    
private:
    std::vector<double> strikes_;
    std::vector<double> expiries_;
    Eigen::MatrixXd implied_vols_;
    
    // Interpolation method
    double bilinear_interpolate(double strike, double expiry) const;
};

/**
 * @brief Implementation of Black-Scholes pricing model
 */
double BlackScholesModel::calculate_price(
    const OptionSpec& option,
    double underlying_price,
    double volatility,
    double risk_free_rate
) const {
    // Basic parameter validation
    if (volatility <= 0.0 || underlying_price <= 0.0) {
        return 0.0;
    }
    
    // Convert time to years
    double T = option.expiry_days / 365.0;
    if (T <= 0.0) {
        // Expired option
        double intrinsic_value = 0.0;
        if (option.type == OptionType::CALL) {
            intrinsic_value = std::max(0.0, underlying_price - option.strike);
        } else {
            intrinsic_value = std::max(0.0, option.strike - underlying_price);
        }
        return intrinsic_value;
    }
    
    // Calculate d1 and d2
    double d1 = calculate_d1(underlying_price, option.strike, risk_free_rate, volatility, T);
    double d2 = calculate_d2(d1, volatility, T);
    
    // Calculate option price
    double price = 0.0;
    if (option.type == OptionType::CALL) {
        price = underlying_price * normal_cdf(d1) - option.strike * std::exp(-risk_free_rate * T) * normal_cdf(d2);
    } else {
        price = option.strike * std::exp(-risk_free_rate * T) * normal_cdf(-d2) - underlying_price * normal_cdf(-d1);
    }
    
    return price * option.lots * option.multiplier;
}

Greeks BlackScholesModel::calculate_greeks(
    const OptionSpec& option,
    double underlying_price,
    double volatility,
    double risk_free_rate
) const {
    // Basic parameter validation
    if (volatility <= 0.0 || underlying_price <= 0.0) {
        return {0.0, 0.0, 0.0, 0.0, 0.0};
    }
    
    // Convert time to years
    double T = option.expiry_days / 365.0;
    if (T <= 0.0) {
        // Expired option
        return {0.0, 0.0, 0.0, 0.0, 0.0};
    }
    
    // Calculate d1 and d2
    double d1 = calculate_d1(underlying_price, option.strike, risk_free_rate, volatility, T);
    double d2 = calculate_d2(d1, volatility, T);
    
    // Calculate option Greeks
    Greeks greeks;
    double sqrt_T = std::sqrt(T);
    double multiplier = option.lots * option.multiplier;
    
    // Delta
    if (option.type == OptionType::CALL) {
        greeks.delta = normal_cdf(d1) * multiplier;
    } else {
        greeks.delta = (normal_cdf(d1) - 1.0) * multiplier;
    }
    
    // Gamma (same for calls and puts)
    greeks.gamma = normal_pdf(d1) / (underlying_price * volatility * sqrt_T) * multiplier;
    
    // Theta
    double theta1 = -underlying_price * normal_pdf(d1) * volatility / (2.0 * sqrt_T);
    double theta2 = risk_free_rate * option.strike * std::exp(-risk_free_rate * T);
    
    if (option.type == OptionType::CALL) {
        greeks.theta = (theta1 - theta2 * normal_cdf(d2)) * multiplier / 365.0;  // Daily theta
    } else {
        greeks.theta = (theta1 + theta2 * normal_cdf(-d2)) * multiplier / 365.0;  // Daily theta
    }
    
    // Vega (same for calls and puts)
    greeks.vega = underlying_price * sqrt_T * normal_pdf(d1) * multiplier / 100.0;  // Per 1% vol change
    
    // Rho
    if (option.type == OptionType::CALL) {
        greeks.rho = option.strike * T * std::exp(-risk_free_rate * T) * normal_cdf(d2) * multiplier / 100.0;  // Per 1% rate change
    } else {
        greeks.rho = -option.strike * T * std::exp(-risk_free_rate * T) * normal_cdf(-d2) * multiplier / 100.0;  // Per 1% rate change
    }
    
    return greeks;
}

double BlackScholesModel::calculate_d1(double S, double K, double r, double sigma, double T) const {
    return (std::log(S / K) + (r + sigma * sigma / 2.0) * T) / (sigma * std::sqrt(T));
}

double BlackScholesModel::calculate_d2(double d1, double sigma, double T) const {
    return d1 - sigma * std::sqrt(T);
}

double OptionPricingModel::normal_cdf(double x) const {
    static const boost::math::normal_distribution<> norm;
    return boost::math::cdf(norm, x);
}

double OptionPricingModel::normal_pdf(double x) const {
    static const boost::math::normal_distribution<> norm;
    return boost::math::pdf(norm, x);
}

} // namespace sim
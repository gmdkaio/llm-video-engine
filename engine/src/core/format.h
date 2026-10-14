#pragma once
#include <cmath>
#include <string>

// Number formatting per locale. pt-BR matches toLocaleString('pt-BR') and the
// reference template's fmtV(); en-US swaps the separators and the currency.
struct Locale {
    std::string currency = "R$ ";  // includes the separator before the amount
    char decimal = ',', thousands = '.';
    std::string days = "dias";
    std::string thousand = " mil", million = " mi";  // suffixes of the mil and mi formats

    static Locale make(const std::string& tag) {
        Locale l;
        if (tag.rfind("en", 0) == 0) { l.currency = "$"; l.decimal = '.'; l.thousands = ','; l.days = "days"; l.thousand = "k"; l.million = "M"; }
        return l;
    }

    std::string nf(double v, int decimals) const {
        const double scale = std::pow(10.0, decimals);
        long long n = std::llround(std::fabs(v) * scale);
        const bool neg = v < 0 && n != 0;
        std::string frac;
        for (int i = 0; i < decimals; ++i) { frac.insert(frac.begin(), char('0' + n % 10)); n /= 10; }
        std::string whole = std::to_string(n);
        for (int i = (int)whole.size() - 3; i > 0; i -= 3) whole.insert((size_t)i, 1, thousands);
        return (neg ? "-" : "") + whole + (decimals ? std::string(1, decimal) + frac : "");
    }

    // format: k, mil, mi, pct, x, days, num. decimals < 0 means the format's default.
    std::string value(double v, const std::string& f, int dec, const std::string& pre = "", const std::string& suf = "") const {
        const std::string sign = v < 0 ? "\xe2\x88\x92" : "";  // U+2212 minus
        const double a = std::fabs(v);
        const int d0 = dec < 0 ? 0 : dec, d1 = dec < 0 ? 1 : dec;
        std::string s;
        if (f == "k") s = currency + nf(a, d0) + "k";
        else if (f == "mil") s = currency + nf(a, d0) + thousand;
        else if (f == "mi") s = currency + nf(a, d1) + million;
        else if (f == "pct") s = nf(a, d1) + "%";
        else if (f == "x") s = nf(a, dec < 0 ? 2 : dec) + "\xc3\x97";  // ×
        else if (f == "days") s = nf(a, d0) + " " + days;
        else s = nf(a, d0);
        return pre + sign + s + suf;
    }
};

// Decimals an axis needs so that every step label is exact.
inline int axisDecimals(double step) {
    if (std::fabs(step - std::round(step)) < 1e-9) return 0;
    if (std::fabs(step * 10 - std::round(step * 10)) < 1e-9) return 1;
    return 2;
}

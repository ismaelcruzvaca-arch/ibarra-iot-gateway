#ifndef EMA_FILTER_H
#define EMA_FILTER_H

class EMAFilter {
public:
    explicit EMAFilter(float alpha = 0.1f) 
        : alpha(alpha), value(0.0f), initialized(false) {}

    float filter(float raw) {
        if (!initialized) {
            value = raw;
            initialized = true;
        } else {
            value = alpha * raw + (1.0f - alpha) * value;
        }
        return value;
    }

    float getValue() const {
        return value;
    }

    void reset() {
        initialized = false;
        value = 0.0f;
    }

    void setAlpha(float newAlpha) {
        alpha = newAlpha;
    }

private:
    float alpha;
    float value;
    bool initialized;
};

#endif // EMA_FILTER_H

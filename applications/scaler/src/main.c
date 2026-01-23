
#include <app/drivers/scaler.h>

int main(void)
{
    const struct device *scaler_dev = DEVICE_DT_GET(DT_NODELABEL(scaler));
    scaler_set(scaler_dev, 0x01, 0xFF);
    return 0;
}
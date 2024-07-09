#include <stm32f4xx_hal.h>

static TIM_HandleTypeDef istanzaTimer = { .Instance = TIM2 };

void initTimer()
{
    __TIM2_CLK_ENABLE();
    istanzaTimer.Init.Prescaler = 40000;
    istanzaTimer.Init.CounterMode = TIM_COUNTERMODE_UP;
    istanzaTimer.Init.Period = 500;
    istanzaTimer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    istanzaTimer.Init.RepetitionCounter = 0;
    HAL_TIM_Base_Init(&istanzaTimer);
    HAL_TIM_Base_Start(&istanzaTimer);
}

void initLed()
{
    __GPIOD_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruttura;
    GPIO_InitStruttura.Pin = GPIO_PIN_12;
    GPIO_InitStruttura.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruttura.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruttura);
}

int main()
{
    HAL_Init();
    initLed();
    initTimer();

    while(1)
    {
        int timer = __HAL_TIM_GET_COUNTER(&istanzaTimer);
        
        if (timer == 500) HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET);
        if (timer == 1000) HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_RESET);
    }
}
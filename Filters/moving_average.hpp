#pragma once

#include <cstdint>

namespace Filters {

// Скользящая сумма окна N > 0: новый отсчёт заменяет самый старый.
// Sample — тип отсчёта, Sum — тип накопителя; Sum должен вмещать сумму всего окна.
// Среднее вычисляется как sum / N. До заполнения окна отсутствующие отсчёты равны нулю.
template<std::uint32_t N, typename Sample = std::int32_t, typename Sum = std::int64_t>
struct MovingAverage {
    Sample array[N]{};     // Кольцевой буфер отсчётов.
    Sum sum{};             // Сумма текущего окна.
    std::uint32_t index{}; // Индекс самого старого отсчёта.

    // Постоянное количество операций независимо от размера окна.
    void add_new_sample(Sample sample)
    {
        sum -= array[index];
        array[index] = sample;
        sum += sample;

        if (++index == N) index = 0;
    }
};

} // namespace Filters

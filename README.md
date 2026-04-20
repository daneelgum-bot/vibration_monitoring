# CMZ_VIBRO
Разработка системы вибромониторинга

$\varepsilon$

# CMZ_VIBRO
Разработка системы вибромониторинга

Данные корректно считываются с датчика и передаются в задачу отправки по сети. 
Поле	Тип	Размер, байт	Описание
sensor_id	uint32_t	4	Айди датчика
rms_speed	float	4	СКЗ виброскорости
accel	массив float	4096	Отсчеты ускорения
spectrum	массив float	2048	Значения спектра виброскорости
ИТОГО 6 152 Байта каждые 0,33 секунды ≈18,3 кб/с



\usepackage{booktabs}
\usepackage{array}

\begin{table}[h]
\centering
\caption{Формат бинарного пакета данных (WebSocket)}
\label{tab:packet_format}
\begin{tabular}{@{}llcl@{}}
\toprule
\textbf{Поле} & \textbf{Тип данных} & \textbf{Размер, байт} & \textbf{Описание} \\
\midrule
\texttt{sensor\_id} & \texttt{uint32\_t} & 4 & Идентификатор датчика (1 или 2) \\
\texttt{rms\_speed} & \texttt{float} & 4 & СКЗ виброскорости, мм/с \\
\texttt{accel} & \texttt{float[1024]} & 4096 & Массив отсчётов модуля виброускорения, g \\
\texttt{spectrum} & \texttt{float[512]} & 2048 & Спектр виброскорости (пиковые значения), мм/с \\
\midrule
\multicolumn{3}{@{}l}{\textbf{Общий размер пакета}} & \textbf{6152 байт} \\
\multicolumn{3}{@{}l}{Период отправки} & $\approx0{,}32$ с (3,125 Гц) \\
\multicolumn{3}{@{}l}{Пропускная способность} & $\approx18{,}8$ кБ/с \\
\bottomrule
\end{tabular}
\end{table}

<img width="327" height="304" alt="иной путь после завода" src="https://github.com/user-attachments/assets/59413a03-ec43-436a-a3c1-4543e8c01071" />

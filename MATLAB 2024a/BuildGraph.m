%==========================================================================
% BuildGraph.m - BMS Test Data Offline Plotting (Script)
%==========================================================================
% Author: Student Research Project (BMS-EV)
% Hardware: STM32F103C8T6 Master | UART telemetry logged by LabVIEW
% Source: ..\Docs\BMS_TESTING.csv (27 fields per row, no header)
%
% CSV layout (1-based MATLAB columns):
%   col  1: time_ms        -> s  (divide by 1000)
%   col  2: total voltage  -> V  (divide by 1000)
%   col  3: SoC            -> %  (divide by 10)
%   col  4: current        -> A  (divide by 1000)
%   col  5: temperature    -> C  (divide by 10)
%   col  6: Vmax (skipped)
%   col  7: Vmin (skipped)
%   col  8: delta voltage  -> mV (raw)
%   col  9-11: unused zeros (skipped)
%   col 12-21: cell 1..10 voltages -> mV (raw)
%   col 22-27: unused zeros + flag (skipped)
%
% Rows that do not have exactly 27 numeric fields are skipped silently
% (e.g. the first row of BMS_TESTING.csv has no time column).
%
% Plots (all vs time in seconds):
%   Total Voltage (V), Cell Voltages 1-10 (mV, with legend),
%   Current (A), Temperature (C), SoC (%), Delta Voltage (mV).
%==========================================================================
clc;
clearvars;
close all;

CSV_PATH = 'D:\Work\Projects\BMS_NCKH2026\Battery-Management-System-EV\Docs\BMS_TESTING.csv';
EXPECTED_FIELDS = 27;

if ~isfile(CSV_PATH)
	error('BuildGraph:FileNotFound', 'CSV file not found: %s', CSV_PATH);
end

fid = fopen(CSV_PATH, 'rt');
if fid == -1
	error('BuildGraph:OpenFailed', 'Cannot open CSV file: %s', CSV_PATH);
end
raw = textscan(fid, '%s', 'Delimiter', '\n', 'Whitespace', '');
fclose(fid);
rawLines = raw{1};

data = zeros(0, EXPECTED_FIELDS);
for k = 1:numel(rawLines)
	ln = strtrim(rawLines{k});
	if isempty(ln)
		continue;
	end
	parts = strsplit(ln, ',');
	if numel(parts) ~= EXPECTED_FIELDS
		continue; % malformed row (e.g. first row missing time column)
	end
	vals = str2double(parts);
	if any(isnan(vals))
		continue;
	end
	data(end + 1, :) = vals; %#ok<AGROW>
end

if isempty(data)
	error('BuildGraph:NoData', 'No valid %d-field rows found in %s', EXPECTED_FIELDS, CSV_PATH);
end

t_s      = data(:, 1) / 1000;
v_tot_v  = data(:, 2) / 1000;
soc_pct  = data(:, 3) / 10;
i_a      = data(:, 4) / 1000;
temp_c   = data(:, 5) / 10;
dv_mv    = data(:, 8);
cells_mv = data(:, 12:21);

fig = figure('Name', 'BMS Test Data - Pack and Cell Graphs', ...
	'NumberTitle', 'off', 'Color', 'w');

ax_total = subplot(3, 2, 1);
plot(ax_total, t_s, v_tot_v, 'Color', [0 0.6 0], 'LineWidth', 1.1);
ylabel(ax_total, 'Total Voltage (V)', 'FontWeight', 'bold');
xlabel(ax_total, 'Time (s)', 'FontWeight', 'bold');
title(ax_total, 'Total Voltage vs Time');
grid(ax_total, 'on');

ax_cells = subplot(3, 2, 2);
hold(ax_cells, 'on');
colors = lines(10);
for c = 1:10
	plot(ax_cells, t_s, cells_mv(:, c), 'Color', colors(c, :), ...
		'LineWidth', 1.0, 'DisplayName', sprintf('Cell %d', c));
end
hold(ax_cells, 'off');
ylabel(ax_cells, 'Cell Voltage (mV)', 'FontWeight', 'bold');
xlabel(ax_cells, 'Time (s)', 'FontWeight', 'bold');
title(ax_cells, 'Cell Voltages vs Time');
grid(ax_cells, 'on');
legend(ax_cells, 'Location', 'best');

ax_curr = subplot(3, 2, 3);
plot(ax_curr, t_s, i_a, 'Color', [0.8 0.6 0], 'LineWidth', 1.1);
ylabel(ax_curr, 'Current (A)', 'FontWeight', 'bold');
xlabel(ax_curr, 'Time (s)', 'FontWeight', 'bold');
title(ax_curr, 'Current vs Time');
grid(ax_curr, 'on');

ax_temp = subplot(3, 2, 4);
plot(ax_temp, t_s, temp_c, 'Color', [0.8 0 0], 'LineWidth', 1.1);
ylabel(ax_temp, 'Temperature (°C)', 'FontWeight', 'bold');
xlabel(ax_temp, 'Time (s)', 'FontWeight', 'bold');
title(ax_temp, 'Temperature vs Time');
grid(ax_temp, 'on');

ax_soc = subplot(3, 2, 5);
plot(ax_soc, t_s, soc_pct, 'Color', [0 0.4 0.8], 'LineWidth', 1.1);
ylabel(ax_soc, 'SoC (%)', 'FontWeight', 'bold');
xlabel(ax_soc, 'Time (s)', 'FontWeight', 'bold');
title(ax_soc, 'SoC vs Time');
grid(ax_soc, 'on');

ax_delta = subplot(3, 2, 6);
plot(ax_delta, t_s, dv_mv, 'Color', [0.6 0 0.6], 'LineWidth', 1.1);
ylabel(ax_delta, 'Delta Voltage (mV)', 'FontWeight', 'bold');
xlabel(ax_delta, 'Time (s)', 'FontWeight', 'bold');
title(ax_delta, 'Delta Voltage vs Time');
grid(ax_delta, 'on');

linkaxes([ax_total, ax_cells, ax_curr, ax_temp, ax_soc, ax_delta], 'x');

fprintf('Plotted %d samples from %s (t = %.3f s .. %.3f s).\n', ...
	size(data, 1), CSV_PATH, t_s(1), t_s(end));

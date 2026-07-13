%==========================================================================
% ScriptMonitor.m - Real-Time Automotive BMS Telemetry Monitor
%==========================================================================
% Author: Student Research Project (BMS-EV)
% Target Hardware: STM32F103C8T6 (Master MCU)
% Communication: UART @ 115200 Baud on COM5
%==========================================================================

% Clear workspace and close previous figures
clc;
clearvars;
close all;

fprintf('====================================================\n');
fprintf('       BMS REAL-TIME UART TELEMETRY MONITOR         \n');
fprintf('====================================================\n');

% Define Configuration Parameters
COM_PORT = "COM5";
BAUD_RATE = 115200;
MAX_POINTS = 100000; % Increased buffer for long experiments

% Register global cleanup to force-release COM_PORT on exit/interrupt
cleanupObj = onCleanup(@() cleanup_serial_port(COM_PORT));

% Initialize serial handle to empty (will connect dynamically in the loop)
s = [];

%==========================================================================
% FIGURE 1: Pack Parameters (New Layout & Style)
%==========================================================================
fig1 = figure('Name', 'BMS Pack Parameters', 'NumberTitle', 'off', 'Color', 'w', 'Position', [50, 50, 900, 800]);

% Create axes and line handles
ax_total = subplot(3, 2, [1, 2], 'Parent', fig1, 'Color', 'w', 'XColor', 'k', 'YColor', 'k');
p_total = plot(ax_total, NaN, NaN, 'Color', [0, 0.6, 0], 'LineWidth', 2);
ylabel(ax_total, 'Total Voltage (V)', 'FontWeight', 'bold');
xlabel(ax_total, 'Time (s)', 'FontWeight', 'bold');
grid(ax_total, 'on'); ax_total.GridColor = 'k'; ax_total.GridAlpha = 0.2;
txt_total = text(ax_total, 0.02, 0.9, '', 'Units', 'normalized', 'FontWeight', 'bold', 'FontSize', 5);

ax_soc = subplot(3, 2, 3, 'Parent', fig1, 'Color', 'w', 'XColor', 'k', 'YColor', 'k');
p_soc = plot(ax_soc, NaN, NaN, 'Color', [0, 0.4, 0.8], 'LineWidth', 2);
ylabel(ax_soc, 'SOC (%)', 'FontWeight', 'bold');
xlabel(ax_soc, 'Time (s)', 'FontWeight', 'bold');
grid(ax_soc, 'on'); ax_soc.GridColor = 'k'; ax_soc.GridAlpha = 0.2;
txt_soc = text(ax_soc, 0.02, 0.9, '', 'Units', 'normalized', 'FontWeight', 'bold', 'FontSize', 5);

ax_curr = subplot(3, 2, 4, 'Parent', fig1, 'Color', 'w', 'XColor', 'k', 'YColor', 'k');
p_curr = plot(ax_curr, NaN, NaN, 'Color', [0.8, 0.6, 0], 'LineWidth', 2);
ylabel(ax_curr, 'Current (A)', 'FontWeight', 'bold');
xlabel(ax_curr, 'Time (s)', 'FontWeight', 'bold');
grid(ax_curr, 'on'); ax_curr.GridColor = 'k'; ax_curr.GridAlpha = 0.2;
txt_curr = text(ax_curr, 0.02, 0.9, '', 'Units', 'normalized', 'FontWeight', 'bold', 'FontSize', 5);

ax_temp = subplot(3, 2, 5, 'Parent', fig1, 'Color', 'w', 'XColor', 'k', 'YColor', 'k');
p_temp = plot(ax_temp, NaN, NaN, 'Color', [0.8, 0, 0], 'LineWidth', 2);
ylabel(ax_temp, 'Temp (°C)', 'FontWeight', 'bold');
xlabel(ax_temp, 'Time (s)', 'FontWeight', 'bold');
grid(ax_temp, 'on'); ax_temp.GridColor = 'k'; ax_temp.GridAlpha = 0.2;
txt_temp = text(ax_temp, 0.02, 0.9, '', 'Units', 'normalized', 'FontWeight', 'bold', 'FontSize', 5);

ax_delta = subplot(3, 2, 6, 'Parent', fig1, 'Color', 'w', 'XColor', 'k', 'YColor', 'k');
p_delta = plot(ax_delta, NaN, NaN, 'Color', [0.6, 0, 0.6], 'LineWidth', 2);
ylabel(ax_delta, 'Delta V (mV)', 'FontWeight', 'bold');
xlabel(ax_delta, 'Time (s)', 'FontWeight', 'bold');
grid(ax_delta, 'on'); ax_delta.GridColor = 'k'; ax_delta.GridAlpha = 0.2;
txt_delta = text(ax_delta, 0.02, 0.9, '', 'Units', 'normalized', 'FontWeight', 'bold', 'FontSize', 5);

linkaxes([ax_total, ax_soc, ax_curr, ax_temp, ax_delta], 'x');

%==========================================================================
% FIGURE 2: 10 Cell Voltages
%==========================================================================
fig2 = figure('Name', 'BMS Cell Voltages', 'NumberTitle', 'off', 'Color', 'w', 'Position', [870, 250, 900, 550]);
axCell = axes('Parent', fig2, 'Color', 'w', 'XColor', 'k', 'YColor', 'k');
hold(axCell, 'on');
ylabel(axCell, 'Voltage (mV)', 'FontWeight', 'bold');
xlabel(axCell, 'Time (s)', 'FontWeight', 'bold');
grid(axCell, 'on'); axCell.GridColor = 'k'; axCell.GridAlpha = 0.2;
colors = lines(10);
p_cells = gobjects(1, 10);
for c = 1:10
    p_cells(c) = plot(axCell, NaN, NaN, 'Color', colors(c, :), 'LineWidth', 1.5, 'DisplayName', sprintf('Cell %d', c));
end
legend(axCell, 'Location', 'eastoutside');
txt_cells = text(axCell, 0.02, 0.95, '', 'Units', 'normalized', 'FontWeight', 'bold', 'FontSize', 5);

% Stop Buttons
setappdata(fig1, 'stop', false); setappdata(fig2, 'stop', false);
uicontrol(fig1, 'Style', 'pushbutton', 'String', 'Stop', 'Position', [20, 10, 60, 30], 'Callback', @(s,e) stop_cb(fig1, fig2));
uicontrol(fig2, 'Style', 'pushbutton', 'String', 'Stop', 'Position', [20, 10, 60, 30], 'Callback', @(s,e) stop_cb(fig1, fig2));

% Data Buffers
t_buf = zeros(1, MAX_POINTS);
v_buf = zeros(1, MAX_POINTS);
soc_buf = zeros(1, MAX_POINTS);
cur_buf = zeros(1, MAX_POINTS);
tmp_buf = zeros(1, MAX_POINTS);
del_buf = zeros(1, MAX_POINTS);
c_buf = zeros(10, MAX_POINTS);
num = 0;
t0 = tic;

while true
    if ~ishandle(fig1) || ~ishandle(fig2) || getappdata(fig1, 'stop'), break; end
    
    % 1. Dynamic Serial Port Connection / Reconnection
    if isempty(s) || ~isvalid(s)
        try
            % Check if the port is available to prevent blocking/erroring
            available_ports = serialportlist("available");
            if ~any(contains(available_ports, COM_PORT))
                set(txt_total, 'String', 'Waiting for COM5 connection...');
                set(txt_soc, 'String', 'Waiting for COM5 connection...');
                set(txt_curr, 'String', 'Waiting for COM5 connection...');
                set(txt_temp, 'String', 'Waiting for COM5 connection...');
                set(txt_delta, 'String', 'Waiting for COM5 connection...');
                set(txt_cells, 'String', 'Waiting for COM5 connection...');
                drawnow limitrate;
                pause(0.2);
                continue;
            end
            
            % Port is available, attempt to connect
            s = serialport(COM_PORT, BAUD_RATE);
            s.Timeout = 0.5;
            flush(s);
            fprintf('Successfully connected to %s at %d baud.\n', COM_PORT, BAUD_RATE);
        catch
            s = [];
            pause(0.2);
            continue;
        end
    end
    
    % 2. Read line data with error handling for disconnection
    try
        lineData = readline(s);
    catch ME
        fprintf('Connection to %s lost: %s. Attempting to reconnect...\n', COM_PORT, ME.message);
        cleanup_serial_port(COM_PORT);
        s = [];
        continue;
    end
    
    if isempty(lineData), continue; end
    
    fields = strsplit(strtrim(char(lineData)), ',');
    if numel(fields) < 20, continue; end
    
    nums = str2double(fields);
    if any(isnan(nums(1:20))), continue; end
    
    num = num + 1;
    t_buf(num) = toc(t0);
    v_buf(num) = nums(1) / 1000;
    soc_buf(num) = nums(2) / 10;
    cur_buf(num) = nums(3) / 1000;
    tmp_buf(num) = nums(4) / 10;
    del_buf(num) = nums(7);
    c_buf(:, num) = nums(11:20);
    
    % Update Plot Data
    set(p_total, 'XData', t_buf(1:num), 'YData', v_buf(1:num));
    set(p_soc, 'XData', t_buf(1:num), 'YData', soc_buf(1:num));
    set(p_curr, 'XData', t_buf(1:num), 'YData', cur_buf(1:num));
    set(p_temp, 'XData', t_buf(1:num), 'YData', tmp_buf(1:num));
    set(p_delta, 'XData', t_buf(1:num), 'YData', del_buf(1:num));
    for c=1:10, set(p_cells(c), 'XData', t_buf(1:num), 'YData', c_buf(c, 1:num)); end
    
    % Auto-scale Y, update X limits dynamically, and Update Labels/Text
    ylim(ax_total, 'auto'); ylim(ax_soc, 'auto'); ylim(ax_curr, 'auto'); ylim(ax_temp, 'auto'); ylim(ax_delta, 'auto'); ylim(axCell, 'auto');
    
    % Update X limits to grow continuously with elapsed time
    x_max = max(10, t_buf(num) * 1.05);
    xlim(ax_delta, [0, x_max]); % linkaxes propagates this to all subplots in Fig 1
    xlim(axCell, [0, x_max]);
    
    set(txt_total, 'String', sprintf('Time: %.1fs | Val: %.2fV', t_buf(num), v_buf(num)));
    set(txt_soc, 'String', sprintf('Time: %.1fs | Val: %.1f%%', t_buf(num), soc_buf(num)));
    set(txt_curr, 'String', sprintf('Time: %.1fs | Val: %.2fA', t_buf(num), cur_buf(num)));
    set(txt_temp, 'String', sprintf('Time: %.1fs | Val: %.1fC', t_buf(num), tmp_buf(num)));
    set(txt_delta, 'String', sprintf('Time: %.1fs | Val: %dmV', t_buf(num), del_buf(num)));
    set(txt_cells, 'String', sprintf('Time: %.1fs | Min: %.0fmV | Max: %.0fmV', t_buf(num), min(c_buf(:, num)), max(c_buf(:, num))));
    
    drawnow limitrate;
end

function stop_cb(f1, f2), if ishandle(f1), setappdata(f1, 'stop', true); end; end

function cleanup_serial_port(port_name)
    try
        old_ports = serialportfind("Port", port_name);
        if ~isempty(old_ports)
            delete(old_ports);
            fprintf('Serial interface %s safely closed.\n', port_name);
        end
    catch
    end
end

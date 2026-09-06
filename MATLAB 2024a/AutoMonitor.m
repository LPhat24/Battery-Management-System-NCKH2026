%==========================================================================
% AutoMonitor.m - Distributed Battery Management System Monitoring (Auto)
%==========================================================================
% Author: Student Research Project (BMS-EV)
% Hardware: STM32F103C8T6 Master  |  UART 115200 (PA9/PA10) - Fixed COM5
% CSV: total_mV, soc_tenths, current_mA, temp_tenths, min_mV, max_mV,
%      deltaV_mV, swSetting, swLoad, swBal, cell1..cell15_mV \n
% SW_UART (PA15) must be ON for telemetry to be transmitted.
% AutoMonitor is hardcoded to COM5 @ 115200. All graph config identical
% to ScriptMonitor.m.
%==========================================================================
clc;
clearvars;
delete(findall(0,'Type','figure'));

fprintf('====================================================\n');
fprintf('  Distributed Battery Management System Monitoring   \n');
fprintf('====================================================\n');

MAX_POINTS = 500000;
DEFAULT_BAUD = '115200';
DEFAULT_DUR  = 'inf';

% Release orphaned MATLAB serial handles from previous crashed sessions so
% ports (e.g. COM5) that Device Manager still lists become openable again.
try
    orphan = serialportfind;
    if ~isempty(orphan)
        delete(orphan);
        fprintf('Released %d orphaned serial handle(s).\n', numel(orphan));
    end
catch
end

%==========================================================================
% FIGURE 1: Pack Parameters (Total V, Current, Temp, SoC, DeltaV) + Control Panel
%==========================================================================
fig1 = figure('Name','Distributed Battery Management System Monitoring', ...
    'NumberTitle','off','Color','w','Units','normalized','Position',[0.005 0.035 0.985 0.925], ...
    'CloseRequestFcn', @(src,evt) onFigureClose(src));

% --- Control Panel docked at top of fig1 (single line) ---
ctrlPanel = uipanel('Parent',fig1,'Title','Control Panel','Units','normalized', ...
    'Position',[0.005 0.925 0.990 0.070],'BackgroundColor','w','FontWeight','bold','FontSize',9);

uicontrol(ctrlPanel,'Style','text','String','Port: COM5 @ 115200 (fixed)','Units','normalized','Position',[0.010 0.25 0.175 0.50],'BackgroundColor','w','FontWeight','bold','FontSize',8,'ForegroundColor',[0 0.45 0]);

uicontrol(ctrlPanel,'Style','text','String','Dur(s)/inf:','Units','normalized','Position',[0.375 0.28 0.070 0.44],'BackgroundColor','w','FontWeight','bold','FontSize',8,'HorizontalAlignment','left');
hEditDur = uicontrol(ctrlPanel,'Style','edit','String',DEFAULT_DUR,'Units','normalized','Position',[0.450 0.25 0.070 0.50],'BackgroundColor','w','FontSize',8,'TooltipString','Number of seconds or inf for continuous');

hBtnStart = uicontrol(ctrlPanel,'Style','pushbutton','String','Start','Units','normalized','Position',[0.195 0.18 0.075 0.62], ...
    'BackgroundColor',[0.2 0.7 0.2],'ForegroundColor','w','FontWeight','bold','FontSize',8, ...
    'Callback', @(s,e) onStart());

hBtnStop = uicontrol(ctrlPanel,'Style','pushbutton','String','Stop','Units','normalized','Position',[0.278 0.18 0.075 0.62], ...
    'BackgroundColor',[0.8 0.2 0.2],'ForegroundColor','w','FontWeight','bold','FontSize',8, ...
    'Callback', @(s,e) onStop());
set(hBtnStop,'Enable','off');

hTxtStatus = uicontrol(ctrlPanel,'Style','text','String','Status: Idle. COM5 @ 115200 fixed. Set Duration, then Start. SW_UART (PA15) must be ON.', ...
    'Units','normalized','Position',[0.540 0.25 0.355 0.50],'BackgroundColor','w','HorizontalAlignment','left','FontSize',7);

hTxtTimer = uicontrol(ctrlPanel,'Style','text','String','Elapsed: 0.0 s','Units','normalized','Position',[0.900 0.28 0.095 0.44], ...
    'BackgroundColor','w','FontWeight','bold','FontSize',8,'HorizontalAlignment','center');

% --- Pack axes: left 50% (5 plots) + right 50% cell voltages (DPI-safe) ---
ax_total = axes('Parent',fig1,'Position',[0.025 0.610 0.450 0.285],'Color','w','XColor','k','YColor','k');
p_total = plot(ax_total, NaN, NaN, 'Color',[0 0.6 0],'LineWidth',1.1);
ylabel(ax_total,'Total Voltage (mV)','FontWeight','bold','FontSize',7);
xlabel(ax_total,'Time (s)','FontWeight','bold','FontSize',7);
grid(ax_total,'on'); ax_total.GridColor='k'; ax_total.GridAlpha=0.2;
ax_total.YAxis.Exponent = 0; ax_total.FontSize = 6;
txt_total = text(ax_total,0.02,0.90,'','Units','normalized','FontWeight','bold','FontSize',6,'BackgroundColor','w','Margin',2);

ax_soc = axes('Parent',fig1,'Position',[0.025 0.330 0.205 0.240],'Color','w','XColor','k','YColor','k');
p_soc = plot(ax_soc, NaN, NaN, 'Color',[0 0.4 0.8],'LineWidth',1.1);
ylabel(ax_soc,'SoC (%)','FontWeight','bold','FontSize',7);
xlabel(ax_soc,'Time (s)','FontWeight','bold','FontSize',7);
grid(ax_soc,'on'); ax_soc.GridColor='k'; ax_soc.GridAlpha=0.2; ax_soc.FontSize = 6;
txt_soc = text(ax_soc,0.02,0.90,'','Units','normalized','FontWeight','bold','FontSize',6,'BackgroundColor','w','Margin',2);

ax_curr = axes('Parent',fig1,'Position',[0.270 0.330 0.205 0.240],'Color','w','XColor','k','YColor','k');
p_curr = plot(ax_curr, NaN, NaN, 'Color',[0.8 0.6 0],'LineWidth',1.1);
ylabel(ax_curr,'Current (A)','FontWeight','bold','FontSize',7);
xlabel(ax_curr,'Time (s)','FontWeight','bold','FontSize',7);
grid(ax_curr,'on'); ax_curr.GridColor='k'; ax_curr.GridAlpha=0.2; ax_curr.FontSize = 6;
txt_curr = text(ax_curr,0.02,0.90,'','Units','normalized','FontWeight','bold','FontSize',6,'BackgroundColor','w','Margin',2);

ax_temp = axes('Parent',fig1,'Position',[0.025 0.050 0.205 0.240],'Color','w','XColor','k','YColor','k');
p_temp = plot(ax_temp, NaN, NaN, 'Color',[0.8 0 0],'LineWidth',1.1);
ylabel(ax_temp,'Temperature (C)','FontWeight','bold','FontSize',7);
xlabel(ax_temp,'Time (s)','FontWeight','bold','FontSize',7);
grid(ax_temp,'on'); ax_temp.GridColor='k'; ax_temp.GridAlpha=0.2; ax_temp.FontSize = 6;
txt_temp = text(ax_temp,0.02,0.90,'','Units','normalized','FontWeight','bold','FontSize',6,'BackgroundColor','w','Margin',2);

ax_delta = axes('Parent',fig1,'Position',[0.270 0.050 0.205 0.240],'Color','w','XColor','k','YColor','k');
p_delta = plot(ax_delta, NaN, NaN, 'Color',[0.6 0 0.6],'LineWidth',1.1);
ylabel(ax_delta,'Delta V (mV)','FontWeight','bold','FontSize',7);
xlabel(ax_delta,'Time (s)','FontWeight','bold','FontSize',7);
grid(ax_delta,'on'); ax_delta.GridColor='k'; ax_delta.GridAlpha=0.2; ax_delta.FontSize = 6;
txt_delta = text(ax_delta,0.02,0.90,'','Units','normalized','FontWeight','bold','FontSize',6,'BackgroundColor','w','Margin',2);

linkaxes([ax_total, ax_soc, ax_curr, ax_temp, ax_delta],'x');

% Cell voltages: right 50% priority, full height
axCell = axes('Parent',fig1,'Position',[0.500 0.050 0.475 0.845],'Color','w','XColor','k','YColor','k');
hold(axCell,'on');
ylabel(axCell,'Cell Voltage (mV)','FontWeight','bold','FontSize',7);
xlabel(axCell,'Time (s)','FontWeight','bold','FontSize',7);
grid(axCell,'on'); axCell.GridColor='k'; axCell.GridAlpha=0.2; axCell.FontSize = 6;
colors = lines(10);
p_cells = gobjects(1,10);
for c = 1:10
    p_cells(c) = plot(axCell, NaN, NaN, 'Color', colors(c,:), 'LineWidth',1.0, 'DisplayName', sprintf('Cell %d',c));
end
legend(axCell,'Location','northeast','FontSize',5.5);
txt_cells = text(axCell,0.02,0.95,'','Units','normalized','FontWeight','bold','FontSize',6,'BackgroundColor','w','Margin',2);

drawnow;

% --- Initialize appdata on fig1 (docked controls) ---
setappdata(fig1,'isRunning',false);
setappdata(fig1,'sHandle',[]);
setappdata(fig1,'hBtnStart',hBtnStart);
setappdata(fig1,'hBtnStop',hBtnStop);
setappdata(fig1,'hEditDur',hEditDur);
setappdata(fig1,'hTxtStatus',hTxtStatus);
setappdata(fig1,'hTxtTimer',hTxtTimer);
setappdata(fig1,'fig1',fig1);
setappdata(fig1,'figCtrl',fig1);
setappdata(fig1,'axHandles',{ax_total, ax_soc, ax_curr, ax_temp, ax_delta, axCell});
setappdata(fig1,'lineHandles',{p_total, p_soc, p_curr, p_temp, p_delta, p_cells});
setappdata(fig1,'txtHandles',{txt_total, txt_soc, txt_curr, txt_temp, txt_delta, txt_cells, hTxtTimer, []});

% Store globally accessible handle for callbacks
assignin('base','gBMSCtrlFig',fig1);
setappdata(0,'BMSCtrlFig',fig1);

fprintf('GUI ready. COM5 @ 115200 fixed. Set Duration, then click Start.\n');
fprintf('Note: Master SW_UART switch (PA15) must be ON for telemetry.\n');

%==========================================================================
% Local Functions
%==========================================================================

function onStart()
    figCtrl = getappdata(0,'BMSCtrlFig');
    if isempty(figCtrl) || ~ishandle(figCtrl), return; end
    if getappdata(figCtrl,'isRunning')
        return;
    end
    hEditDur = getappdata(figCtrl,'hEditDur');
    hTxtStatus=getappdata(figCtrl,'hTxtStatus');
    hBtnStart=getappdata(figCtrl,'hBtnStart');
    hBtnStop=getappdata(figCtrl,'hBtnStop');
    try
        % Hardcoded COM5 @ 115200 for AutoMonitor
        comStr = "COM5";
        baudVal = 115200;
        durStr = strtrim(get(hEditDur,'String'));
        if strcmpi(durStr,'inf') || strcmpi(durStr,'infinite') || durStr==""
            durationSec = inf;
        else
            durationSec = str2double(durStr);
            if isnan(durationSec) || durationSec<0
                set(hTxtStatus,'String','Status: Invalid Duration. Use number or inf.');
                return;
            end
            if durationSec==0
                durationSec = inf;
            end
        end

        cleanupSerial(comStr);
        s = [];
        try
            s = serialport(comStr, baudVal);
            s.Timeout = 1.0;
            configureTerminator(s,"LF");
            flush(s);
        catch ME
            freePorts = serialportlist("available");
            if ~any(strcmpi(strtrim(string(freePorts)), char(comStr)))
                set(hTxtStatus,'String',sprintf('Status: %s is BUSY. Close the app holding it (terminal/LabVIEW/old MATLAB).',comStr));
            else
                set(hTxtStatus,'String',sprintf('Status: Failed to open %s: %s',comStr,ME.message));
            end
            fprintf('onStart open failed: %s\n', ME.getReport());
            return;
        end

        setappdata(figCtrl,'sHandle',s);
        setappdata(figCtrl,'isRunning',true);
        setappdata(figCtrl,'durationSec',durationSec);
        setappdata(figCtrl,'byteTotal',0);
        setappdata(figCtrl,'lineTotal',0);
        set(hTxtStatus,'String',sprintf('Status: Connected %s @ %d baud | Duration: %s | SW_UART must be ON',comStr,baudVal,durStr));
        if ishandle(hBtnStart), set(hBtnStart,'Enable','off'); end
        if ishandle(hBtnStop), set(hBtnStop,'Enable','on'); end
        drawnow;

        resetPlots(figCtrl);
        runAcquisition(figCtrl);
    catch ME
        fprintf('onStart error: %s\n', ME.getReport());
        try
            if ishandle(hTxtStatus)
                set(hTxtStatus,'String',sprintf('Status: Start failed: %s', ME.message));
            end
        catch
        end
        try
            setappdata(figCtrl,'isRunning',false);
            sTmp = getappdata(figCtrl,'sHandle');
            if ~isempty(sTmp)
                try, pn=sTmp.Port; cleanupSerial(pn); catch, cleanupSerial(""); end
                setappdata(figCtrl,'sHandle',[]);
            end
        catch
        end
        try
            if ishandle(hBtnStart), set(hBtnStart,'Enable','on'); end
            if ishandle(hBtnStop), set(hBtnStop,'Enable','off'); end
        catch
        end
    end
end

function onStop()
    figCtrl = getappdata(0,'BMSCtrlFig');
    if isempty(figCtrl) || ~ishandle(figCtrl), return; end
    setappdata(figCtrl,'isRunning',false);
    hTxtStatus = getappdata(figCtrl,'hTxtStatus');
    hBtnStart = getappdata(figCtrl,'hBtnStart');
    hBtnStop = getappdata(figCtrl,'hBtnStop');
    s = getappdata(figCtrl,'sHandle');
    if ~isempty(s)
        try
            pn = s.Port;
            cleanupSerial(pn);
        catch
            cleanupSerial("");
        end
        setappdata(figCtrl,'sHandle',[]);
    end
    if ishandle(hTxtStatus)
        curStr = get(hTxtStatus,'String');
        set(hTxtStatus,'String',sprintf('%s | Stopped.',curStr));
    end
    if ishandle(hBtnStart), set(hBtnStart,'Enable','on'); end
    if ishandle(hBtnStop), set(hBtnStop,'Enable','off'); end
    fprintf('Acquisition stopped by user.\n');
end

function onFigureClose(src)
    figCtrl = getappdata(0,'BMSCtrlFig');
    if ~isempty(figCtrl) && src==figCtrl
        try
            setappdata(figCtrl,'isRunning',false);
            s = getappdata(figCtrl,'sHandle');
            if ~isempty(s)
                try, pn=s.Port; cleanupSerial(pn); catch, cleanupSerial(""); end
            end
        catch
        end
        try, delete(figCtrl); catch, end
        setappdata(0,'BMSCtrlFig',[]);
        return;
    end
    if ~isempty(figCtrl) && ishandle(figCtrl)
        setappdata(figCtrl,'isRunning',false);
    end
    try, delete(src); catch, end
end

function resetPlots(figCtrl)
    axHandles = getappdata(figCtrl,'axHandles');
    lineHandles = getappdata(figCtrl,'lineHandles');
    txtHandles = getappdata(figCtrl,'txtHandles');
    for k=1:5
        set(lineHandles{k},'XData',NaN,'YData',NaN);
    end
    p_cells = lineHandles{6};
    for c=1:10
        set(p_cells(c),'XData',NaN,'YData',NaN);
    end
    set(txtHandles{1},'String','');
    set(txtHandles{2},'String','');
    set(txtHandles{3},'String','');
    set(txtHandles{4},'String','');
    set(txtHandles{5},'String','');
    set(txtHandles{6},'String','');
    for k=1:6
        if ishandle(axHandles{k})
            xlim(axHandles{k},[0 10]);
            ylim(axHandles{k},'auto');
        end
    end
    drawnow;
end

function runAcquisition(figCtrl)
    hTxtStatus = getappdata(figCtrl,'hTxtStatus');
    hTxtTimer  = getappdata(figCtrl,'hTxtTimer');
    axHandles  = getappdata(figCtrl,'axHandles');
    lineHandles= getappdata(figCtrl,'lineHandles');
    txtHandles = getappdata(figCtrl,'txtHandles');
    fig1 = getappdata(figCtrl,'fig1');

    ax_total = axHandles{1}; ax_soc=axHandles{2}; ax_curr=axHandles{3};
    ax_temp=axHandles{4}; ax_delta=axHandles{5}; axCell=axHandles{6};
    p_total=lineHandles{1}; p_soc=lineHandles{2}; p_curr=lineHandles{3};
    p_temp=lineHandles{4}; p_delta=lineHandles{5}; p_cells=lineHandles{6};
    txt_total=txtHandles{1}; txt_soc=txtHandles{2}; txt_curr=txtHandles{3};
    txt_temp=txtHandles{4}; txt_delta=txtHandles{5}; txt_cells=txtHandles{6};

    s = getappdata(figCtrl,'sHandle');
    durationSec = getappdata(figCtrl,'durationSec');

    MAXP = 500000;
    t_buf = zeros(1,MAXP); v_buf=zeros(1,MAXP); soc_buf=zeros(1,MAXP);
    cur_buf=zeros(1,MAXP); tmp_buf=zeros(1,MAXP); del_buf=zeros(1,MAXP);
    c_buf=zeros(10,MAXP);
    num = 0;
    t0 = tic;
    byteTotal = 0;
    lineTotal = 0;
    swLab = -1;

    try
    while true
        if ~ishandle(figCtrl) || ~getappdata(figCtrl,'isRunning')
            if ~getappdata(figCtrl,'isRunning')
                fprintf('Acquisition stopped by user (lines=%d, bytes=%d).\n', lineTotal, byteTotal);
            end
            break;
        end
        if ~ishandle(fig1)
            fprintf('Acquisition stopped: figure closed (lines=%d, bytes=%d).\n', lineTotal, byteTotal);
            setappdata(figCtrl,'isRunning',false);
            break;
        end
        elapsed = toc(t0);
        if isfinite(durationSec) && elapsed >= durationSec
            set(hTxtStatus,'String',sprintf('Status: Duration %.1f s reached. Stopped.',durationSec));
            fprintf('Acquisition stopped: duration %.1f s reached (lines=%d, bytes=%d).\n', durationSec, lineTotal, byteTotal);
            setappdata(figCtrl,'isRunning',false);
            break;
        end
        set(hTxtTimer,'String',sprintf('Elapsed: %.1f s',elapsed));

        if isempty(s) || ~isvalid(s)
            pause(0.05);
            drawnow limitrate;
            s = getappdata(figCtrl,'sHandle');
            continue;
        end

        % Blocking line read - same proven approach as TestUartSending.m
        ln = "";
        try
            ln = strtrim(readline(s));
            byteTotal = byteTotal + strlength(ln) + 1;
        catch ME
            isTimeout = contains(ME.message, "Timeout", "IgnoreCase", true) || ...
                        contains(ME.identifier, "Timeout", "IgnoreCase", true);
            if isTimeout
                if num==0 && elapsed>2
                    set(txt_total,'String','Waiting for data... Check SW_UART (PA15) ON');
                    set(hTxtStatus,'String',sprintf('Status: %s @ %d baud | bytes: %d | lines: %d | Waiting... SW_UART must be ON', s.Port, s.BaudRate, byteTotal, lineTotal));
                end
                drawnow limitrate;
                continue;
            else
                set(hTxtStatus,'String',sprintf('Status: Read error on %s: %s',s.Port,ME.message));
                pause(0.05);
                drawnow limitrate;
                continue;
            end
        end

        if strlength(ln)==0
            drawnow limitrate;
            continue;
        end

        fields = strsplit(ln, ',');
        if numel(fields) < 20
            drawnow limitrate;
            continue;
        end
        nums = str2double(fields);
        if any(isnan(nums(1:20)))
            drawnow limitrate;
            continue;
        end
        lineTotal = lineTotal + 1;
        num = num + 1;
        if num > MAXP
            keep = floor(MAXP*0.6);
            t_buf(1:keep) = t_buf(end-keep+1:end); v_buf(1:keep)=v_buf(end-keep+1:end);
            soc_buf(1:keep)=soc_buf(end-keep+1:end); cur_buf(1:keep)=cur_buf(end-keep+1:end);
            tmp_buf(1:keep)=tmp_buf(end-keep+1:end); del_buf(1:keep)=del_buf(end-keep+1:end);
            c_buf(:,1:keep)=c_buf(:,end-keep+1:end);
            num = keep+1;
        end
        t_buf(num) = toc(t0);
        v_buf(num) = nums(1);
        soc_buf(num) = nums(2)/10;
        cur_buf(num) = nums(3)/1000;
        tmp_buf(num) = nums(4)/10;
        del_buf(num) = nums(7);
        c_buf(:,num) = nums(11:20);
        if numel(fields) >= 26 && isfinite(nums(26))
            swLab = nums(26);
        end

        xData = t_buf(1:num);
        set(p_total,'XData',xData,'YData',v_buf(1:num));
        set(p_soc,'XData',xData,'YData',soc_buf(1:num));
        set(p_curr,'XData',xData,'YData',cur_buf(1:num));
        set(p_temp,'XData',xData,'YData',tmp_buf(1:num));
        set(p_delta,'XData',xData,'YData',del_buf(1:num));
        for c=1:10
            set(p_cells(c),'XData',xData,'YData',c_buf(c,1:num));
        end

        tNow = xData(end);
        xMax = max(1, tNow*1.02);
        xMin = 0;
        xlim(ax_delta,[xMin xMax]);
        xlim(axCell,[xMin xMax]);
        ylim(ax_total,'auto'); ylim(axCell,'auto');
        % Keep total-voltage Y ticks as integers (no x10^n exponent)
        yLim_total = ylim(ax_total);
        pad_total = max((yLim_total(2) - yLim_total(1)) * 0.05, 100);
        ylim(ax_total, [floor((yLim_total(1) - pad_total)/50)*50, ceil((yLim_total(2) + pad_total)/50)*50]);
        % Dynamic Y scaling for remaining pack plots (avoids fixed 0-1 on flat data)
        applyScaledYLim(ax_soc,   soc_buf(1:num), 5);
        applyScaledYLim(ax_curr,  cur_buf(1:num), 0.5);
        applyScaledYLim(ax_temp,  tmp_buf(1:num), 5);
        applyScaledYLim(ax_delta, del_buf(1:num), 2);

        set(txt_total,'String',sprintf('%.0f mV | %.1f s',v_buf(num),tNow));
        set(txt_soc,'String',sprintf('%.1f %% | %.1f s',soc_buf(num),tNow));
        set(txt_curr,'String',sprintf('%.3f A | %.1f s',cur_buf(num),tNow));
        set(txt_temp,'String',sprintf('%.1f C | %.1f s',tmp_buf(num),tNow));
        set(txt_delta,'String',sprintf('%.0f mV | %.1f s',del_buf(num),tNow));
        set(txt_cells,'String',sprintf('Min %.0f mV | Max %.0f mV | %.1f s',min(c_buf(:,num)),max(c_buf(:,num)),tNow));

        if swLab >= 0
            swTxt = "OFF";
            if swLab==1, swTxt = "ON"; end
            set(hTxtStatus,'String',sprintf('Status: %s @ %d baud | bytes: %d | lines: %d | SW_UART: %s', ...
                s.Port, s.BaudRate, byteTotal, lineTotal, swTxt));
        else
            set(hTxtStatus,'String',sprintf('Status: %s @ %d baud | bytes: %d | lines: %d',...
                s.Port, s.BaudRate, byteTotal, lineTotal));
        end

        drawnow limitrate;
    end
    catch ME
        fprintf('Acquisition error: %s\n%s\n', ME.message, ME.getReport());
        if ishandle(hTxtStatus)
            set(hTxtStatus,'String',sprintf('Status: Acquisition error: %s',ME.message));
        end
    end

    setappdata(figCtrl,'byteTotal',byteTotal);
    setappdata(figCtrl,'lineTotal',lineTotal);
    s2 = getappdata(figCtrl,'sHandle');
    if ~isempty(s2)
        try
            pn = s2.Port; cleanupSerial(pn);
        catch
            cleanupSerial("");
        end
        setappdata(figCtrl,'sHandle',[]);
    end
    setappdata(figCtrl,'isRunning',false);
    try
        hBtnStart=getappdata(figCtrl,'hBtnStart');
        hBtnStop=getappdata(figCtrl,'hBtnStop');
        if ishandle(hBtnStart), set(hBtnStart,'Enable','on'); end
        if ishandle(hBtnStop), set(hBtnStop,'Enable','off'); end
    catch
    end
    if ishandle(hTxtTimer)
        set(hTxtTimer,'String',sprintf('Elapsed: %.1f s | Stopped',toc(t0)));
    end
    fprintf('Acquisition ended (lines=%d, bytes=%d).\n', lineTotal, byteTotal);
end

function applyScaledYLim(ax, data, minSpan)
    d = data(isfinite(data));
    if isempty(d)
        ylim(ax, 'auto');
        return;
    end
    lo = min(d);
    hi = max(d);
    span = max(hi - lo, minSpan);
    pad = span * 0.15;
    lo = lo - pad;
    hi = hi + pad;
    step = niceStep(hi - lo);
    ylim(ax, [floor(lo/step)*step, ceil(hi/step)*step]);
end

function step = niceStep(span)
    span = max(span, 1e-9);
    mag = 10^floor(log10(span));
    for c = [1 2 5 10]
        s = c * mag;
        if span <= 5 * s
            step = s;
            return;
        end
    end
    step = 10 * mag;
end

function cleanupSerial(portName)
    try
        if strlength(portName)>0
            old = serialportfind("Port",portName);
        else
            old = serialportfind;
        end
        if ~isempty(old)
            delete(old);
        end
    catch
    end
end

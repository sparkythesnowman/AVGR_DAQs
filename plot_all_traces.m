function plot_all_traces(folder_path)

    files = dir(fullfile(folder_path, '*.bin'));
    for i = 1:length(files)
        file_path = fullfile(folder_path, files(i).name);
        fprintf('Processing file: %s\n', files(i).name);
        read_daq_minimal(file_path);
        set(gca, 'linewidth', 1.5);
        hold on;
        legend_entries{i} = files(i).name; %#ok<AGROW>
    end
    legend(legend_entries, 'Interpreter', 'none');

end
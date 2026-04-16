function samples = read_daq_bin(filename, numSamples)
%READ_DAQ_BIN  Import RP2040 PIO DAQ capture from CAP_xxxxx.bin
%
%   [samples, bitMatrix] = read_daq_bin('CAP_00000.bin', numSamples)
%
%   Assumptions (matches your current firmware):
%   - 8 capture pins (8-bit bus), CAPTURE_PIN_COUNT = 8
%   - PIO config: IN PINS,8 with shift-right, autopush at 32 bits
%   - Each 32-bit word from the Pico contains 4 samples:
%       word = (s0 << 24) | (s1 << 16) | (s2 << 8) | (s3)
%     where s0 is earliest in time.
%   - Data is written as little-endian uint32_t.
%
%   Output:
%     samples   : [numSamples x 1] uint8 vector, values 0..255
%     bitMatrix : [numSamples x 8] logical matrix, bitMatrix(:,1) is D0 (LSB).
    if nargin < 2
        numSamples = [];  % will auto-set to max available
    end
    % --- read raw uint32 words from file (little-endian) ---
    fid = fopen(filename, 'rb');
    if fid == -1
        error('Could not open file "%s"', filename);
    end
    words = fread(fid, Inf, 'uint32=>uint32', 'ieee-le');
    fclose(fid);
    if isempty(words)
        error('File "%s" appears to be empty or not a valid binary capture.', filename);
    end
    % For 8 pins, bits_packed_per_word = 32 => 4 samples per word
    pinCount = 8;
    samplesPerWord = 32 / pinCount;  % = 4
    totalSamplesAvailable = numel(words) * samplesPerWord;
    if isempty(numSamples) || numSamples > totalSamplesAvailable
        numSamples = totalSamplesAvailable;
    end
    % --- unpack words into bytes (samples) ---
    % words are 32-bit. With shift-right + autopush at 32 bits, the PIO placed:
    %   s0 in bits 31:24 (MSB),
    %   s1 in bits 23:16,
    %   s2 in bits 15: 8,
    %   s3 in bits  7: 0 (LSB).
    %
    % On disk (little endian), each word is stored as bytes [s3 s2 s1 s0],
    % so to reconstruct chronological samples we reverse each 4-byte group.
    % First, reinterpret the uint32 vector as a uint8 vector
    bytes = typecast(words, 'uint8');  % little-endian layout
    % Reshape into [nWords x 4] where each row is [b0 b1 b2 b3] = [s3 s2 s1 s0]
    bytesPerWord = 4;
    bytes = reshape(bytes, bytesPerWord, []).';  % rows = words: [b0 b1 b2 b3]
    % Reverse each row to get [s0 s1 s2 s3]
    wordSamples = fliplr(bytes);  % [s0 s1 s2 s3] per word
    % Flatten into chronological sample stream: s0,s1,s2,s3, s0_2,s1_2,...
    allSamples = wordSamples.';       % [4 x nWords]
    allSamples = allSamples(:);       % column vector
    % Trim to requested numSamples
    samples = allSamples(1:numSamples);
    % --- build bit matrix: each column is one data pin (bit) ---
    % bitMatrix = false(numSamples, pinCount);
    % for b = 0:pinCount-1
    %     % bitget: bit 1 is LSB. So b=0 -> D0 (LSB), b=7 -> D7 (MSB).
    %     bitMatrix(:, b+1) = bitget(samples, b+1);
    % end
    % --- (optional) quick preview plot for first N samples ---
    % Nplot = min(numSamples, 64);  % plot first 64 samples by default
    % figure;
    % t = 0:Nplot-1;
    % offset = (0:pinCount-1);  % vertical offsets for each bit
    % plotData = double(bitMatrix(1:Nplot, :)) + offset;  % stack bits
    % plot(t, plotData, '.-');
    % xlabel('Sample index');
    % ylabel('Bit value (offset by channel)');
    % yticks(offset);
    % yticklabels(arrayfun(@(b) sprintf('D%d', b), 0:pinCount-1, 'UniformOutput', false));
    % grid on;
    % title(sprintf('%s: first %d samples', filename, Nplot));
end
function [samples] = read_daq_minimal(filename)
    % --- read raw uint32 words from file (little-endian) ---
    numSamples = [];  % will auto-set to max available
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
    % Optional: plot samples
    plot(linspace(0,numSamples/100e6,numSamples),double(samples)/255*1.9+0.1);
    xlabel('Sample number');
    ylabel('Bit values (0-255)');
    grid on;
    title(sprintf('%s', filename));
end
# -*- coding: utf-8 -*-
"""
Decode binary USD log to csv file
"""
import cfusdlog
import argparse
import csv

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("filename")
    args = parser.parse_args()

    # decode binary log data
    data = cfusdlog.decode(args.filename)
    
    for config in data.keys():
        imu_data = data[config]

        keys = imu_data.keys()
        rows_of_data = zip(*(imu_data[key] for key in keys))

        with open(f'imu_{config}.csv', 'w', ) as csv_file:
            wr = csv.writer(csv_file)
            wr.writerow(keys)
            wr.writerows(rows_of_data)

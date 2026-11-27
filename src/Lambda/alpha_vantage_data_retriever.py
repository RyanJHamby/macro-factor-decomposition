import os
import requests
import boto3
import json
from datetime import date

class AlphaVantageDataRetriever:
    def __init__(self, api_key):
        self.api_key = api_key
        
    def fetch_data_and_upload_to_s3(self, api_url, bucket_name, object_key):
        api_url = f"{api_url}"
        results = self.retrieve(api_url)
        self.upload_to_s3(bucket_name, object_key, results)

    def retrieve(self, api_url):
        try:
            response = requests.get(api_url)
            response.raise_for_status()  # Raise an exception for HTTP errors
            return response.text
        except requests.exceptions.RequestException as e:
            return f"Request failed: {e}"
            
    def upload_to_s3(self, bucket_name, object_key, data):
        try:
            s3 = boto3.client('s3')

            if isinstance(data, str):
                s3.put_object(Bucket=bucket_name, Key=object_key, Body=data)
            elif isinstance(data, bytes):
                s3.put_object(Bucket=bucket_name, Key=object_key, Body=data)
            else:
                raise ValueError("Data must be a string or bytes")

            print(f"Data successfully uploaded to S3: s3://{bucket_name}/{object_key}")
            return True
        except Exception as e:
            print(f"Failed to upload data to S3: {e}")
            return False
        
def fetch(event, context):
    api_key = os.environ["ALPHA_VANTAGE_API_KEY"]
    bucket_name = "alpha-insights"
    data_retriever = AlphaVantageDataRetriever(api_key)
    
    json_file_path = "AlphaVantageConstants.json"
    with open(json_file_path, 'r') as file:
        api_constants = json.load(file)
        for key, val in api_constants.items():
            object_key = val["s3_object_key_prefix"] + "/" + str(date.today())
            stock_data = data_retriever.fetch_data_and_upload_to_s3(val["api_template_url"], bucket_name, object_key)
